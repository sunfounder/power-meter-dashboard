// WS stream test: connect, request stream, count chunks, watch for drops
const IP = process.argv[2] || '192.168.100.243';
const CH = parseInt(process.argv[3] || '0');

const ws = new WebSocket('ws://' + IP + '/ws');
let chunks = 0, bytes = 0, t0 = 0, last = 0;
let got = 0;

ws.onopen = () => {
  console.log('[T] connected at', new Date().toISOString());
  t0 = Date.now(); last = t0;
  // request settings + stream
  ws.send(JSON.stringify({type:'cmd',cmd:'get_settings',req_id:1}));
  ws.send(JSON.stringify({type:'cmd',cmd:'stream_start',ch:CH,offset:0,req_id:2}));
};

ws.onmessage = (e) => {
  if (typeof e.data === 'string') {
    const m = JSON.parse(e.data);
    if (m.type === 'ack') console.log('[T] ack', m.cmd, 'ok=' + m.ok, 'total=' + m.total, 'at', (Date.now()-t0)+'ms');
    else if (m.type === 'stream_done') {
      console.log('[T] DONE total=' + m.total, 'chunks=' + chunks, 'bytes=' + bytes, 'elapsed=' + (Date.now()-t0) + 'ms');
      ws.close(1000);
      process.exit(0);
    }
    else if (m.type === 'measurement') { /* ignore */ }
  } else {
    // binary chunk
    chunks++;
    bytes += e.data.byteLength;
    got += e.data.byteLength / 24;
    const now = Date.now();
    if (chunks % 10 === 0 || now - last > 500) {
      console.log('[T] chunk#' + chunks + ' got=' + got + ' bytes=' + bytes + ' t=' + (now - t0) + 'ms');
      last = now;
    }
  }
};

ws.onerror = (e) => console.log('[T] WS error', e.message || e.type);
ws.onclose = (e) => {
  console.log('[T] CLOSED code=' + e.code + ' reason=' + e.reason, 'chunks=' + chunks, 'got=' + got, 'at', (Date.now()-t0)+'ms');
  process.exit(1);
};

// safety timeout
setTimeout(() => { console.log('[T] TIMEOUT after 30s, chunks=' + chunks + ' got=' + got); process.exit(2); }, 30000);
