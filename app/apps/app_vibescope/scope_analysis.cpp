/**
 * @file scope_analysis.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "scope_analysis.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <mutex>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "dsps_fft2r.h"
#endif

namespace vibescope {

std::array<SpectrumPeak, kMaxSpectrumPeaks> spectrum_peaks(const std::array<const Analysis*, 3>& spectra,
                                                          std::size_t first, std::size_t count, unsigned limit)
{
    std::array<SpectrumPeak, kMaxSpectrumPeaks> peaks{};
    if (!count || !limit) return peaks;
    std::array<float, 3> floors{};
    for (unsigned axis = 0; axis < spectra.size(); ++axis) {
        if (!spectra[axis] || spectra[axis]->spectrum.size() < 9) continue;
        const auto& bins = spectra[axis]->spectrum;
        std::array<float, 63> noise{};
        const auto n = std::min(noise.size(), bins.size() - 1);
        for (std::size_t i = 0; i < n; ++i) noise[i] = bins[1 + i * (bins.size() - 1) / n];
        auto mid = noise.begin() + n / 2;
        std::nth_element(noise.begin(), mid, noise.begin() + n);
        // Require a clear tone above the broadband floor and typical Hann sidelobes.
        floors[axis] = std::max({1e-6F, 5.F * *mid, .04F * *std::max_element(bins.begin() + 1, bins.end())});
    }
    for (unsigned rank = 0; rank < std::min(limit, kMaxSpectrumPeaks); ++rank) {
        auto& best = peaks[rank];
        for (unsigned axis = 0; axis < spectra.size(); ++axis) {
            if (!floors[axis]) continue;
            const auto& bins = spectra[axis]->spectrum;
            if (first >= bins.size()) continue;
            const auto end = first + std::min(count, bins.size() - first);
            for (auto i = std::max<std::size_t>(first, 1); i < end; ++i) {
                const float peak = bins[i];
                if (peak <= std::max(best.amplitude, floors[axis]) || peak <= bins[i - 1] ||
                    (i + 1 < bins.size() && peak < bins[i + 1])) continue;
                const auto nearby = std::any_of(peaks.begin(), peaks.begin() + rank, [i](const SpectrumPeak& other) {
                    return std::max(i, other.bin) - std::min(i, other.bin) <= 3;
                });
                if (nearby) continue;
                float background = 0;
                unsigned neighbours = 0;
                for (std::size_t offset = 3; offset <= 8; ++offset) {
                    if (i > offset) {
                        background += bins[i - offset];
                        ++neighbours;
                    }
                    if (i + offset < bins.size()) {
                        background += bins[i + offset];
                        ++neighbours;
                    }
                }
                if (neighbours && peak * neighbours > 2.5F * background) best = {i, axis, peak};
            }
        }
        if (!best.bin) break;
    }
    return peaks;
}
namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr auto kMaxFft = kScopeFftZoomLimit;

// Shift before summation to retain sub-mg signals on a 1 g offset. All hot
// arithmetic uses the P4's single-precision FPU, with compensated reductions.
struct Sum {
    float value{}, error{};
    void add(float input)
    {
        const float corrected = input - error;
        const float next = value + corrected;
        error = (next - value) - corrected;
        value = next;
    }
};

void bit_reverse(float* data, std::size_t n)
{
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(data[2 * i], data[2 * j]);
            std::swap(data[2 * i + 1], data[2 * j + 1]);
        }
    }
}

// The same bit-reversed twiddle layout as ESP-DSP's radix-2 kernel. Its prefix
// supports every smaller power of two, so changing FFT size needs no new roots.
void make_twiddles(float* table)
{
    for (std::size_t i = 0; i < kMaxFft / 2; ++i) {
        const float phase = 2 * kPi * static_cast<float>(i) / kMaxFft;
        table[2 * i] = std::cos(phase);
        table[2 * i + 1] = std::sin(phase);
    }
    bit_reverse(table, kMaxFft / 2);
}

void portable_fft(float* data, std::size_t n, const float* roots)
{
    for (std::size_t half = n / 2, groups = 1; half; half >>= 1, groups <<= 1) {
        std::size_t base = 0;
        for (std::size_t group = 0; group < groups; ++group) {
            const float c = roots[2 * group], s = roots[2 * group + 1];
            for (std::size_t i = 0; i < half; ++i, ++base) {
                const std::size_t a = 2 * base, b = 2 * (base + half);
                const float re = c * data[b] + s * data[b + 1];
                const float im = c * data[b + 1] - s * data[b];
                data[b] = data[a] - re;
                data[b + 1] = data[a + 1] - im;
                data[a] += re;
                data[a + 1] += im;
            }
            base += half;
        }
    }
    bit_reverse(data, n);
}

struct FftWorkspace {
    std::mutex mutex;
    void* allocation{};
    float *bins{}, *window{}, *roots{};
    std::size_t window_n{};
    float window_sum{}, window_power{};
    bool native{};

    FftWorkspace()
    {
        constexpr std::size_t bytes = 4 * kMaxFft * sizeof(float);
#ifdef ESP_PLATFORM
        allocation = heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        bins = static_cast<float*>(allocation);
#endif
        if (!bins) {
            // Portable fallback can use PSRAM. Explicitly check allocation: ESP
            // builds disable exceptions, so a failed vector allocation aborts.
            allocation = std::malloc(bytes + 15);
            if (!allocation) return;
            const auto aligned = (reinterpret_cast<std::uintptr_t>(allocation) + 15) & ~std::uintptr_t{15};
            bins = reinterpret_cast<float*>(aligned);
        }
        window = bins + 2 * kMaxFft;
        roots = window + kMaxFft;
#if defined(ESP_PLATFORM) && CONFIG_IDF_TARGET_ESP32P4 && CONFIG_DSP_OPTIMIZED
        // arp4_ uses ordinary aligned float loads/stores and also accepts
        // cache-backed PSRAM when internal SRAM is too fragmented for 32 KiB.
        native = true;
        // Private tables avoid global init/deinit ownership and the library's
        // optional RAM bit-reversal allocation. arp4_ accepts the table directly.
        if (native && (dsps_gen_w_r2_fc32(roots, kMaxFft) != ESP_OK ||
                       dsps_bit_rev_fc32_ansi(roots, kMaxFft / 2) != ESP_OK)) native = false;
#endif
        if (!native) make_twiddles(roots);
#ifdef ESP_PLATFORM
        ESP_LOGI("scope_dsp", "FFT backend=%s, aligned workspace=%uKiB", native ? "esp-dsp 1.8.2 arp4" : "portable",
             static_cast<unsigned>(bytes >> 10));
#endif
    }
    ~FftWorkspace() { std::free(allocation); }
    void prepare_window(std::size_t n)
    {
        if (window_n == n) return;
        Sum sum, power;
        for (std::size_t i = 0; i < n; ++i) {
            window[i] = .5F - .5F * std::cos(2 * kPi * static_cast<float>(i) / static_cast<float>(n - 1));
            sum.add(window[i]);
            power.add(window[i] * window[i]);
        }
        window_sum = sum.value;
        window_power = power.value;
        window_n = n;
    }
};
}

Analysis analyze(const std::vector<Axes>& samples, float rate_hz, std::size_t axis, std::size_t stats_count,
                 std::size_t spectrum_bins)
{
    Analysis result;
    const auto count = stats_count ? std::min(stats_count, samples.size()) : samples.size();
    if (count < 8 || !std::isfinite(rate_hz) || rate_hz <= 0 || axis >= 3) return result;
    const auto first = samples.size() - count;
    const float reference = samples[first][axis];
    Sum shifted;
    for (auto i = first; i < samples.size(); ++i) {
        if (!std::isfinite(samples[i][axis])) return result;
        shifted.add(samples[i][axis] - reference);
    }
    const float mean = shifted.value / static_cast<float>(count);
    Sum sum2, sum4;
    float low = 0, high = 0;
    for (auto i = first; i < samples.size(); ++i) {
        const float value = (samples[i][axis] - reference) - mean;
        low               = std::min(low, value);
        high              = std::max(high, value);
        const float square = value * value;
        sum2.add(square);
        sum4.add(square * square);
    }
    result.valid        = true;
    result.sample_count = count;
    const float variance = sum2.value / static_cast<float>(count);
    result.rms          = std::sqrt(variance);
    result.peak         = std::max(-low, high);
    result.peak_to_peak = high - low;
    if (result.rms > 1e-9F) {
        result.crest    = result.peak / result.rms;
        result.kurtosis = (sum4.value / static_cast<float>(count)) / (variance * variance);
    }
    std::size_t n = 8;
    // Zoom only raises the cap; the default window stays at kScopeFftLimit so
    // broadband plots keep their cost and transient response.
    const auto cap = spectrum_bins > kScopeFftLimit ? std::min(kScopeFftZoomLimit, spectrum_bins) : kScopeFftLimit;
    while (n * 2 <= samples.size() && n < cap) n *= 2;
    static FftWorkspace workspace;
    std::lock_guard<std::mutex> lock(workspace.mutex);
    if (!workspace.bins) return result;  // statistics remain available under memory pressure
    workspace.prepare_window(n);
    float* bins = workspace.bins;
    Sum fft_shifted;
    const std::size_t start = samples.size() - n;
    const float fft_reference = samples[start][axis];
    for (std::size_t i = start; i < samples.size(); ++i) {
        if (i < first && !std::isfinite(samples[i][axis])) return {};
        fft_shifted.add(samples[i][axis] - fft_reference);
    }
    const float fft_mean = fft_shifted.value / static_cast<float>(n);
    for (std::size_t i = 0; i < n; ++i) {
        bins[2 * i] = ((samples[start + i][axis] - fft_reference) - fft_mean) * workspace.window[i];
        bins[2 * i + 1] = 0;
    }
    bool transformed = false;
#if defined(ESP_PLATFORM) && CONFIG_IDF_TARGET_ESP32P4 && CONFIG_DSP_OPTIMIZED
    if (workspace.native) {
        transformed = dsps_fft2r_fc32_arp4_(bins, static_cast<int>(n), workspace.roots) == ESP_OK;
        if (transformed) bit_reverse(bins, n);
        else {
            workspace.native = false;
            for (std::size_t i = 0; i < n; ++i) {
                bins[2 * i] = ((samples[start + i][axis] - fft_reference) - fft_mean) * workspace.window[i];
                bins[2 * i + 1] = 0;
            }
        }
    }
#endif
    if (!transformed) portable_fft(bins, n, workspace.roots);
    result.bin_hz = rate_hz / static_cast<float>(n);
    result.spectrum.resize(n / 2 + 1);
    std::size_t maximum = 0;
    float maximum_power = 0;
    std::array<Sum, kRmsBandCount> band_power{};
    std::size_t band = 0;
    for (std::size_t i = 1; i <= n / 2; ++i) {
        const float power = bins[2 * i] * bins[2 * i] + bins[2 * i + 1] * bins[2 * i + 1];
        const float magnitude = std::sqrt(power);
        result.spectrum[i] = magnitude * (i == n / 2 ? 1.F : 2.F) / workspace.window_sum;
        // Frequency follows one-sided power (2x interior, 1x Nyquist), not
        // squared peak amplitude (4x interior). This resolves both Nyquist and
        // its adjacent bin correctly despite their overlapping Hann lobes.
        const float one_sided_power = power * (i == n / 2 ? 1.F : 2.F);
        const float hz = static_cast<float>(i) * result.bin_hz;
        if (hz >= kRmsBandEdgesHz.front() && hz <= kRmsBandEdgesHz.back()) {
            while (band + 1 < kRmsBandCount && hz >= kRmsBandEdgesHz[band + 1]) ++band;
            band_power[band].add(one_sided_power);
        }
        if (one_sided_power > maximum_power) {
            maximum_power = one_sided_power;
            maximum = i;
        }
    }
    // Parseval with Hann mean-square correction. Peak-spectrum normalization
    // uses coherent gain instead and must not be used for summing bin power.
    const float power_normalizer = static_cast<float>(n) * workspace.window_power;
    for (std::size_t b = 0; b < kRmsBandCount; ++b) {
        result.band_valid[b] = rate_hz * .5F >= kRmsBandEdgesHz[b + 1] &&
                               result.bin_hz <= kRmsBandEdgesHz[b];
        if (result.band_valid[b])
            result.band_rms[b] = std::sqrt(band_power[b].value / power_normalizer);
    }
    result.frequency = static_cast<float>(maximum) * result.bin_hz;
    return result;
}

static float vector_power(const Axes& value, const Axes& mean)
{
    const float x = value[0] - mean[0], y = value[1] - mean[1], z = value[2] - mean[2];
    return x * x + y * y + z * z;
}

void resultant_waveform(const std::vector<Axes>& samples, const Axes& mean, std::vector<float>& values)
{
    values.resize(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) values[i] = std::sqrt(vector_power(samples[i], mean));
}

Analysis analyze_resultant(const std::vector<Axes>& samples, std::size_t stats_count,
                           const std::array<Analysis, 3>& axes, std::vector<float>* waveform)
{
    Analysis result;
    if (!axes[0].valid || !axes[1].valid || !axes[2].valid) return result;
    const auto count = stats_count ? std::min(stats_count, samples.size()) : samples.size();
    if (!count) return result;
    const auto first = samples.size() - count;
    Axes mean{};
    for (unsigned a = 0; a < 3; ++a) {
        Sum shifted;
        const auto reference = samples[first][a];
        for (auto i = first; i < samples.size(); ++i) shifted.add(samples[i][a] - reference);
        mean[a] = reference + shifted.value / static_cast<float>(count);
    }
    Sum sum2, sum4;
    float peak = 0, low = std::numeric_limits<float>::infinity();
    for (auto i = first; i < samples.size(); ++i) {
        const auto power = vector_power(samples[i], mean);
        sum2.add(power);
        sum4.add(power * power);
        peak = std::max(peak, power);
        low = std::min(low, power);
    }
    result.valid = true;
    result.sample_count = count;
    const auto power = sum2.value / static_cast<float>(count);
    result.rms = std::sqrt(power);
    result.peak = std::sqrt(peak);
    result.peak_to_peak = result.peak - std::sqrt(low);
    if (result.rms > 1e-9F) {
        result.crest = result.peak / result.rms;
        result.kurtosis = (sum4.value / static_cast<float>(count)) / (power * power);
    }
    result.bin_hz = axes[0].bin_hz;
    const auto bins = std::min({axes[0].spectrum.size(), axes[1].spectrum.size(), axes[2].spectrum.size()});
    result.spectrum.resize(bins);
    float strongest = 0;
    for (std::size_t i = 1; i < bins; ++i) {
        const auto bin = vector_power({axes[0].spectrum[i], axes[1].spectrum[i], axes[2].spectrum[i]}, {});
        result.spectrum[i] = std::sqrt(bin);
        const auto power_weighted = i + 1 == bins ? bin * 2.F : bin;
        if (power_weighted > strongest) {
            strongest = power_weighted;
            result.frequency = static_cast<float>(i) * result.bin_hz;
        }
    }
    for (unsigned b = 0; b < kRmsBandCount; ++b) {
        result.band_valid[b] = axes[0].band_valid[b] && axes[1].band_valid[b] && axes[2].band_valid[b];
        if (result.band_valid[b])
            result.band_rms[b] = std::sqrt(vector_power({axes[0].band_rms[b], axes[1].band_rms[b], axes[2].band_rms[b]}, {}));
    }
    if (waveform) resultant_waveform(samples, mean, *waveform);
    return result;
}

float convert_accel(std::int32_t count, std::size_t axis, bool adxl, const vhm::probe::CalibrationRecord& cal,
                    unsigned range_g)
{
    if (axis >= 3) return 0;
    if (!range_g) range_g = adxl ? kAdxlRangesG[0] : kIcmRangesG[3];
    float value       = static_cast<float>(count) * (range_g / (adxl ? 524288.F : 32768.F));
    const auto flag   = adxl ? 1U : 2U;
    const auto& bias  = adxl ? cal.adxl_accel_bias_g : cal.icm_accel_bias_g;
    const auto& scale = adxl ? cal.adxl_accel_scale : cal.icm_accel_scale;
    if ((cal.valid_flags & flag) && std::isfinite(bias[axis]) && std::isfinite(scale[axis]) && scale[axis] > .1F &&
        scale[axis] < 10.F)
        value = (value - bias[axis]) * scale[axis];
    return value;
}

float convert_gyro(std::int32_t count, std::size_t axis, const vhm::probe::CalibrationRecord& cal)
{
    if (axis >= 3) return 0;
    float value = static_cast<float>(count) / 65.536F;  // frozen preset: +/-500 dps
    if ((cal.valid_flags & 4U) && std::isfinite(cal.icm_gyro_bias_dps[axis])) value -= cal.icm_gyro_bias_dps[axis];
    return value;
}

bool scope_can_display(vhm::Quality quality)
{
    constexpr auto fatal = vhm::Quality::CrcError | vhm::Quality::TimeUncertain | vhm::Quality::SequenceGap |
                           vhm::Quality::FormatError | vhm::Quality::FifoOverflow | vhm::Quality::QueueOverflow;
    return !vhm::has_quality(quality, fatal);
}

void SampleHistory::clear()
{
    _size = 0;
    _head = 0;
    _have_near_full_scale = false;
    ++_generation;
}
void SampleHistory::push(Axes sample, std::uint32_t segment, std::uint32_t config, std::uint64_t sequence,
                         bool near_full_scale)
{
    if (_size && (_segment != segment || _config != config || _next != sequence)) clear();
    _segment        = segment;
    _config         = config;
    _next           = sequence + 1;
    if (near_full_scale) {
        _last_near_full_scale = sequence;
        _have_near_full_scale = true;
    }
    _samples[_head] = sample;
    _head           = (_head + 1) % kCapacity;
    _size           = std::min(_size + 1, kCapacity);
}
bool SampleHistory::near_full_scale(std::size_t count) const
{
    count = std::min(count, _size);
    return count && _have_near_full_scale && _next - _last_near_full_scale <= count;
}
std::vector<Axes> SampleHistory::copy(std::size_t count) const
{
    count = std::min(count, _size);
    std::vector<Axes> output(count);
    for (std::size_t i = 0; i < count; ++i) output[i] = _samples[(_head + kCapacity - count + i) % kCapacity];
    return output;
}
}  // namespace vibescope
