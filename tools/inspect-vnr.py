# @file inspect-vnr.py
# @author 煲仔卤煮(ZealerLu)
# @date 2026-11-02
#
# @copyright Copyright (c) 2026
#
"""Inspect a VibraScope raw recording, optionally exporting calibrated physical samples.

Usage: python tools/inspect-vnr.py capture.vnr [--csv samples.csv]
"""
import argparse
import csv
import struct
from pathlib import Path


def read_capture(path):
    with Path(path).open('rb') as stream:
        magic=stream.read(8)
        if magic[:5]!=b'VNRAW' or len(magic)!=8 or magic[5] not in (1,2,3):
            raise ValueError('Not a supported VibraScope recording')
        flags=0
        calibration=(0.0,)*16
        if magic[5]>=2:
            header=stream.read(72)
            if len(header)!=72: raise ValueError('Truncated calibration header')
            flags,_generation,*calibration=struct.unpack('<II16f',header)
        while True:
            header=stream.read(40)
            if not header: return
            if len(header)!=40: raise ValueError('Truncated batch header')
            kind,config,segment,quality,sequence,time,period,count=struct.unpack('<IIIIQQII',header)
            if kind not in (1,2) or not 1<=count<=108: raise ValueError('Invalid batch')
            range_g=10 if kind==1 else 16
            if magic[5]==3:
                raw_range=stream.read(4)
                if len(raw_range)!=4: raise ValueError('Truncated acceleration range')
                range_g,=struct.unpack('<I',raw_range)
                if range_g not in ((10,20,40) if kind==1 else (2,4,8,16)):
                    raise ValueError('Invalid acceleration range')
            body=stream.read(count*28)
            if len(body)!=count*28: raise ValueError('Truncated sample payload')
            for i in range(count):
                raw=struct.unpack_from('<7i',body,i*28)
                values=[]
                for axis in range(3):
                    nominal=(524288.0 if kind==1 else 32768.0)/range_g
                    value=raw[axis]/nominal
                    offset=0 if kind==1 else 6
                    scale=calibration[offset+3+axis]
                    if flags&(1 if kind==1 else 2) and .1<scale<10:
                        value=(value-calibration[offset+axis])*scale
                    values.append(value)
                for axis in range(3):
                    values.append(raw[axis+3]/65.536-(calibration[12+axis] if flags&4 else 0) if kind==2 else '')
                yield (kind,config,segment,quality,sequence+i,time+round(i*period/65536),*values,raw[6])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('--csv',type=Path)
    args=parser.parse_args()
    counts={1:0,2:0}
    output=args.csv.open('w',newline='',encoding='utf-8') if args.csv else None
    try:
        writer=csv.writer(output) if output else None
        if writer:writer.writerow(['stream','config','segment','quality','sequence','time_us','ax_g','ay_g','az_g','gx_dps','gy_dps','gz_dps','temperature_raw'])
        for row in read_capture(args.capture):
            counts[row[0]]+=1
            if writer:writer.writerow(row)
    finally:
        if output:output.close()
    print(f'ADXL samples: {counts[1]}; ICM samples: {counts[2]}')


if __name__=='__main__':main()
