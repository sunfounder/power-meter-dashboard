// List .dat files via WS, then stream the first available channel
const IP = process.argv[2] || '192.168.100.243';
const ws = new WebSocket('ws://' + IP + '/ws');
let files = [];
let streamed = false;

ws.onopen = () => {
  ws.send(JSON.stringify({type:'cmd',cmd:'files',req_id:1}));
};

ws.onmessage = (e) => {
  if (typeof e.data !== 'string') {
    if (streamed) {
      console.log('[T] chunk +' + e.data.byteLength + 'B');
    }
    return;
  }
  const m = JSON.parse(e.data);
  if (m.type === 'ack' && m.cmd === 'files') {
    files = m.data || [];
    console.log('[T] files:', files.map(f => f.name + ' (' + f.size + 'B)').join(', '));
    if (!files.length) { console.log('[T] no files'); process.exit(0); }
    // stream the largest file's channel from its filename (test_chN_...)
    const big = files.reduce((a, b) => a.size > b.size ? a : b);
    const chm = big.name.match(/ch(\d)/);
    const ch = chm ? parseInt(chm[1]) - 1 : 0;
    console.log('[T] streaming ch' + ch + ' file=' + big.name);
    ws.send(JSON.stringify({type:'cmd',cmd:'stream_start',ch,offset:0,req_id:2}));
  } else if (m.type === 'ack' && m.cmd === 'stream_start') {
    console.log('[T] stream_start ok=' + m.ok + ' total=' + m.total);
    if (!m.ok) { console.log('[T] stream failed'); process.exit(1); }
    streamed = true;
  } else if (m.type === 'stream_done') {
    console.log('[T] DONE total=' + m.total + ' elapsed=' + (Date.now() - t0) + 'ms');
    process.exit(0);
  }
};

let t0 = Date.now();
ws.onclose = (e) => {
  console.log('[T] CLOSED code=' + e.code + ' at ' + (Date.now() - t0) + 'ms');
  process.exit(1);
};
setTimeout(() => { console.log('[T] TIMEOUT'); process.exit(2); }, 30000);
