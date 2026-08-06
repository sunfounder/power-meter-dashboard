// Test download_start: fetch the largest file, verify bytes received
const IP = process.argv[2] || '192.168.100.243';
const ws = new WebSocket('ws://' + IP + '/ws');
let chunks = 0, totalBytes = 0, t0 = 0;

ws.onopen = () => {
  ws.send(JSON.stringify({type:'cmd',cmd:'files',req_id:1}));
};
ws.onmessage = (e) => {
  if (typeof e.data === 'string') {
    const m = JSON.parse(e.data);
    if (m.type === 'ack' && m.cmd === 'files') {
      const big = m.data.reduce((a,b)=>a.size>b.size?a:b);
      console.log('[T] downloading', big.name, big.size+'B');
      t0 = Date.now();
      ws.send(JSON.stringify({type:'cmd',cmd:'download_start',file:big.name}));
    } else if (m.type === 'dl_done') {
      console.log('[T] DONE bytes=' + totalBytes + ' chunks=' + chunks + ' elapsed=' + (Date.now()-t0) + 'ms');
      process.exit(0);
    } else if (m.type === 'dl_fail') {
      console.log('[T] FAIL', m.error);
      process.exit(1);
    }
  } else {
    chunks++;
    totalBytes += e.data.byteLength;
  }
};
ws.onclose = (e) => { console.log('[T] CLOSED code=' + e.code + ' bytes=' + totalBytes + ' at ' + (Date.now()-t0) + 'ms'); process.exit(1); };
setTimeout(() => { console.log('[T] TIMEOUT bytes=' + totalBytes + ' chunks=' + chunks); process.exit(2); }, 30000);
