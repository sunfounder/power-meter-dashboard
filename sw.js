const CACHE = 'power-meter-v1';
self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE).then(c => c.addAll(['.','index.html','docs.html','manifest.json'])));
  self.skipWaiting();
});
self.addEventListener('activate', e => { clients.claim(); });
self.addEventListener('fetch', e => {
  e.respondWith(caches.match(e.request).then(r => r || fetch(e.request).then(resp => {
    if(resp.ok){const clone=resp.clone();caches.open(CACHE).then(c => c.put(e.request, clone));}
    return resp;
  })));
});
self.addEventListener('message', e => {
  if(e.data==='check-update'){
    fetch('index.html?t='+Date.now()).then(r => r.text()).then(txt => clients.matchAll().then(clients => {
      clients.forEach(c => c.postMessage({type:'update-check', current:t, cached:''}));
    }));
  }
});
