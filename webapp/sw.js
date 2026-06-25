// Service worker — caches the app shell for offline use.
// Only registers in a secure context (HTTPS or localhost); over plain HTTP it
// never runs, which is fine — the app still works, just without offline.
const CACHE = 'epaper-v1';
const SHELL = ['./', 'index.html', 'manifest.webmanifest',
               'icon-192.png', 'icon-512.png', 'apple-touch-icon.png'];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE).then(c => c.addAll(SHELL)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', e => {
  e.waitUntil(caches.keys()
    .then(keys => Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k))))
    .then(() => self.clients.claim()));
});

self.addEventListener('fetch', e => {
  const path = new URL(e.request.url).pathname;
  // Never cache the live device API — always hit the network.
  if (/(\/status\.json|\/(set|weather|station|clock|next|cycle|clear|refresh))/.test(path)) return;
  // App shell: cache-first, fall back to network.
  e.respondWith(caches.match(e.request).then(r => r || fetch(e.request)));
});
