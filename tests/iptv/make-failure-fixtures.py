"""Generate synthetic colour/silence inputs; no downloaded media."""
from pathlib import Path
import subprocess,sys
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
cases=[('bad-video','320x180','stereo','mpeg2video','aac'),
       ('bad-audio','320x180','stereo','libx264','ac3'),
       ('too-large','1920x1080','stereo','libx264','aac'),
       ('multichannel','320x180','5.1','libx264','aac')]
for name,size,channels,video,audio in cases:
    cmd=['ffmpeg','-hide_banner','-loglevel','error','-y',
         '-f','lavfi','-i','color=c=blue:s='+size+':r=25',
         '-f','lavfi','-i','anullsrc=r=48000:cl='+channels,'-t','2',
         '-c:v',video,'-g','25','-pix_fmt','yuv420p','-c:a',audio]
    if video=='libx264':cmd+=['-preset','ultrafast','-profile:v','main']
    cmd+=['-f','hls','-hls_time','1','-hls_list_size','0',
          '-hls_segment_filename',str(out/(name+'-%d.ts')),str(out/(name+'.m3u8'))]
    subprocess.run(cmd,check=True)
(out/'not-playlist.m3u8').write_text('<html><body>Not a media playlist</body></html>')
print('Generated five incompatible-input fixtures in',out)
