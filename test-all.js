// Full WS API test suite
const IP = '192.168.100.243';
const ws = new WebSocket('ws://' + IP + '/ws');
const results = [];
let rid = 0;
const pending = {};

// Connection timeout guard
const connGuard = setTimeout(() => { console.log('[T] WS connect timeout'); process.exit(4); }, 15000);

function req(cmd, payload, timeout = 8000) {
  return new Promise((res) => {
    const id = ++rid;
    pending[id] = { res, cmd };
    ws.send(JSON.stringify({ type:'cmd', cmd, req_id:id, ...payload }));
    setTimeout(() => { if (pending[id]) { pending[id].res({ timeout: true }); delete pending[id]; } }, timeout);
  });
}

function log(name, ok, detail) {
  results.push({ name, ok, detail });
  console.log((ok ? 'PASS' : 'FAIL') + '  ' + name + (detail ? '  →  ' + detail : ''));
}

let t0 = Date.now();
let dlBytes = 0, dlChunks = 0, streamBytes = 0, streamChunks = 0, streamDone = false;

ws.onopen = async () => {
  clearTimeout(connGuard);
  try {
    // 1. get_settings
    const s = await req('get_settings');
    log('get_settings', s && !s.timeout && s.version, s && 'v' + s.version + ' temp=' + s.temp_unit + ' stop_cond=' + (s.stop_cond ? s.stop_cond.length : 0));

    // 2. files
    const f = await req('files');
    const fl = f && f.data ? f.data : [];
    log('files', Array.isArray(fl), fl.length + ' files');

    // 3. storage
    const st = await req('storage');
    log('storage', st && st.free_kb > 0, st && 'free=' + st.free_kb + 'KB used=' + st.used_kb + 'KB');

    // 4. record_start / record_stop (test file)
    const rs = await req('record_start', { ch: 0, name: '__apitest__' });
    log('record_start', rs && rs.ok === true, 'ch0');
    await new Promise(r => setTimeout(r, 2500));
    const rp = await req('record_stop', { ch: 0 });
    log('record_stop', rp && rp.ok === true, 'ch0');
    await new Promise(r => setTimeout(r, 500));

    // 5. find test file + delete it
    const f2 = await req('files');
    const testFile = (f2 && f2.data || []).find(x => x.name.includes('__apitest__'));
    if (testFile) {
      const del = await req('delete', { file: testFile.name });
      log('delete', del && del.ok === true, testFile.name);
    } else {
      log('delete', false, 'test file not found');
    }

    // 6. stream_start on largest file — measure
    const f3 = await req('files');
    const fl3 = (f3 && f3.data) || [];
    let big = null;
    if (fl3.length) {
      big = fl3.reduce((a,b) => a.size > b.size ? a : b);
      log('stream_start (largest ' + big.name + ' ' + big.size + 'B)', true, '');
      const ss = await req('stream_start', { ch: 0, file: big.name });
      log('stream_start ack', ss && ss.ok === true, ss && 'total=' + ss.total);
      if (ss && ss.ok) streamActive = true;
      // chunks arrive async; wait for done via onmessage flag (timeout 20s)
      await waitFor(() => streamDone, 20000);
      streamActive = false;
      log('stream transfer', streamDone, streamChunks + ' chunks, ' + streamBytes + 'B, ' + (Date.now()-t0) + 'ms');
    } else {
      log('stream_start', false, 'no files');
    }

      // 7. download_start (same file)
      if (big) {
        dlBytes = 0; dlChunks = 0;
        const d0 = Date.now();
        ws.send(JSON.stringify({ type:'cmd', cmd:'download_start', file: big.name }));
        await waitFor(() => dlBytes >= big.size || dlDone, 20000);
        log('download', dlBytes === big.size, dlChunks + ' chunks, ' + dlBytes + '/' + big.size + 'B, ' + (Date.now()-d0) + 'ms');
      }

    // 8. alarm/unknown cmd
    const u = await req('nonexistent_cmd');
    log('unknown cmd handled', u && u.ok === false, 'ok=false');

    // 9. restart (last)
    const rr = await req('restart');
    log('restart', true, 'device restarting');

    console.log('\n===== SUMMARY =====');
    const fails = results.filter(r => !r.ok);
    console.log('Total: ' + results.length + ', Pass: ' + (results.length - fails.length) + ', Fail: ' + fails.length);
    if (fails.length) fails.forEach(f => console.log('  FAIL: ' + f.name));
    process.exit(fails.length ? 1 : 0);
  } catch (e) {
    console.log('TEST ERROR:', e.message);
    process.exit(3);
  }
};

let dlDone = false;
function waitFor(fn, ms) {
  return new Promise(res => {
    const t = setInterval(() => { if (fn()) { clearInterval(t); clearTimeout(to); res(); } }, 200);
    const to = setTimeout(() => { clearInterval(t); res(); }, ms);
  });
}

ws.onmessage = (e) => {
  if (typeof e.data === 'string') {
    const m = JSON.parse(e.data);
    if (m.req_id && pending[m.req_id]) { pending[m.req_id].res(m); delete pending[m.req_id]; }
    if (m.type === 'dl_done') dlDone = true;
    if (m.type === 'stream_done') { streamDone = true; streamTotal = m.total; }
  } else {
    if (streamActive) { streamChunks++; streamBytes += e.data.byteLength; }
    else { dlChunks++; dlBytes += e.data.byteLength; }
  }
};
let streamActive = false;
ws.onclose = (e) => { console.log('[T] WS CLOSED code=' + e.code); };
