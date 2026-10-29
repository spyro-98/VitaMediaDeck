"""Synthetic 1080p/9-Mbps HLS and independent Italian/English AAC tracks."""
from pathlib import Path
import subprocess,sys
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
for name,size,rate in [('sd','640x360','900k'),('hd','1920x1080','9M')]:
 subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size={size}:rate=25','-t','6','-an','-c:v','libx264','-preset','ultrafast','-profile:v','main','-g','25','-bf','0','-b:v',rate,'-minrate',rate,'-maxrate',rate,'-bufsize',rate,'-x264-params','nal-hrd=cbr','-f','hls','-hls_time','1','-hls_list_size','0','-hls_segment_filename',str(out/f'{name}-%d.ts'),str(out/f'{name}.m3u8')],check=True)
for name,frequency,rate in [('it',440,48000),('en',880,44100)]:
 subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i',f'sine=frequency={frequency}:sample_rate={rate}','-t','6','-vn','-c:a','aac','-ac','2','-b:a','128k','-metadata:s:a:0',f'language={name}','-f','hls','-hls_time','1','-hls_list_size','0','-hls_segment_filename',str(out/f'{name}-%d.ts'),str(out/f'{name}.m3u8')],check=True)
(out/'index.m3u8').write_text('#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",NAME="Italiano",LANGUAGE="it",DEFAULT=YES,URI="it.m3u8"\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",NAME="English",LANGUAGE="en",URI="en.m3u8"\n#EXT-X-STREAM-INF:BANDWIDTH=1000000,RESOLUTION=640x360,CODECS="avc1.4d401f,mp4a.40.2",AUDIO="audio"\nsd.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=9200000,RESOLUTION=1920x1080,CODECS="avc1.4d4028,mp4a.40.2",AUDIO="audio"\nhd.m3u8\n')
subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=640x360:rate=25','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-f','lavfi','-i','sine=frequency=880:sample_rate=44100','-t','6','-map','0:v','-map','1:a','-map','2:a','-c:v','libx264','-preset','ultrafast','-g','25','-bf','0','-c:a','aac','-ac','2','-metadata:s:a:0','language=ita','-metadata:s:a:1','language=eng','-f','hls','-hls_time','1','-hls_list_size','0','-hls_segment_filename',str(out/'embedded-%d.ts'),str(out/'embedded.m3u8')],check=True)
print(out)
