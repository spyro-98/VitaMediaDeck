import hashlib,json,subprocess,threading,urllib.parse,sys
from pathlib import Path
binary_dir=Path(sys.argv[1])
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
hits=[]
raw=bytes((i*13+17)%251 for i in range(3*1024*1024+173))
class Trap(BaseHTTPRequestHandler):
 def do_GET(self): hits.append(self.path);self.send_response(200);self.end_headers()
 def log_message(self,*a):pass
trap=ThreadingHTTPServer(('127.0.0.1',0),Trap)
class API(BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def send(self,data,status=200,location=None):
  if isinstance(data,(dict,list)):data=json.dumps(data).encode()
  self.send_response(status);self.send_header('Content-Length',str(len(data)))
  if location:self.send_header('Location',location)
  self.end_headers()
  try:self.wfile.write(data)
  except (BrokenPipeError,ConnectionResetError):pass
 def do_HEAD(self):
  assert '/rest/stream.view?' in self.path
  self.send_response(200);self.send_header('Content-Length',str(len(raw)));self.end_headers()
 def do_GET(self):
  if '/rest/stream.view?' in self.path:
   q=urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
   assert q['t']==[hashlib.md5(('password'+q['s'][0]).encode()).hexdigest()]
   span=self.headers['Range'].split('=')[1];start,end=map(int,span.split('-'));end=min(end,len(raw)-1)
   self.send_response(206);self.send_header('Content-Length',str(end-start+1));self.send_header('Content-Range',f'bytes {start}-{end}/{len(raw)}');self.end_headers()
   try:self.wfile.write(raw[start:end+1])
   except (BrokenPipeError,ConnectionResetError):pass
   return
  u=urllib.parse.urlsplit(self.path)
  if '/rest/' in u.path:
   q=urllib.parse.parse_qs(u.query);assert q['u']==['user'];assert q['t']==[hashlib.md5(('password'+q['s'][0]).encode()).hexdigest()];assert 'x-api-key' not in self.headers
   response={'status':'ok'}
   if 'getAlbumList2' in u.path:response['albumList2']={'album':[{'id':'album1','name':'Mock album'}]}
   self.send({'subsonic-response':response});return
  assert self.headers['x-api-key']=='mock-key';assert 'mock-key' not in self.path
  if self.path.startswith('/redirect'):self.send(b'',302,f'http://127.0.0.1:{trap.server_port}/trap');return
  if self.path.startswith('/oversize'):self.send(b'x'*(4*1024*1024+64));return
  if self.path=='/api/users/me':self.send({'id':'user1'});return
  if self.path=='/api/assets/photo1/thumbnail?size=preview':self.send(b'\xff\xd8\xff');return
  self.send(b'not found',404)
 def do_POST(self):
  assert self.path=='/api/search/metadata';assert self.headers['x-api-key']=='mock-key'
  body=json.loads(self.rfile.read(int(self.headers['Content-Length'])));assert body['type']=='IMAGE' and body['page']==1 and body['size']==100
  self.send({'assets':{'items':[{'id':'photo1','type':'IMAGE','originalFileName':'photo.heic'}],'nextPage':None}})
server=ThreadingHTTPServer(('127.0.0.1',0),API)
for s in [server,trap]:threading.Thread(target=s.serve_forever,daemon=True).start()
try:
 subprocess.run([str(binary_dir/'http-smoke'),f'http://127.0.0.1:{server.server_port}'],check=True)
 subprocess.run([str(binary_dir/'range-test'),f'http://127.0.0.1:{server.server_port}'],check=True)
 assert not hits,'API key leaked across redirect'
 print('Independent redirect target received zero requests')
finally:
 server.shutdown();trap.shutdown()
