const CACHE = 'power-meter-v2';
self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE).then(c => c.addAll(['.','index.html','docs.html','manifest.json'])));
  self.skipWaiting();
});
self.addEventListener('activate', e => {
  // Clean up old caches on upgrade
  e.waitUntil(caches.keys().then(keys => Promise.all(
    keys.filter(k => k !== CACHE).map(k => caches.delete(k))
  )));
  clients.claim();
});
self.addEventListener('fetch', e => {
  const url = new URL(e.request.url);
  // HTML navigations: network-first (always get latest page)
  if (e.request.mode === 'navigate' || url.pathname.endsWith('/index.html') || url.pathname.endsWith('/')) {
    e.respondWith(
      fetch(e.request).then(resp => {
        const clone = resp.clone();
        caches.open(CACHE).then(c => c.put(e.request, clone));
        return resp;
      }).catch(() => caches.match(e.request))
    );
    return;
  }
  // Static assets: cache-first with background refresh
  e.respondWith(caches.match(e.request).then(r => r || fetch(e.request).then(resp => {
    if (resp.ok) { const clone = resp.clone(); caches.open(CACHE).then(c => c.put(e.request, clone)); }
    return resp;
  })));
});
