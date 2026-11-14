# Third-party baseline

This firmware starts from the M5Stack Tab5 factory demo and keeps its UI and board-support dependencies in-tree for reproducible Desktop and ESP32-P4 builds.

| Dependency | Version | Revision/source |
| --- | --- | --- |
| M5Tab5-UserDemo | 0.4 | source archive |
| mooncake | v2.1.0 | `0dfc177b72fcc55095fd222b0bdc69595b001d9f` |
| mooncake_log | v1.0.0 | `41d00d9d7209668215cea09dbfb0cbac36b5a1c0` |
| smooth_ui_toolkit | v2.0.0 | `3a749e5b7090c6aba81729cf556b7b88cb42ce54` |
| lvgl | v9.2.2 | `7f07a129e8d77f4984fff8e623fd5be18ff42e74` |

The imported baseline excludes `.git/`, `build/`, `.superpowers/`, generated `sdkconfig`, `sdkconfig.old`, and `platforms/tab5/managed_components/`.
