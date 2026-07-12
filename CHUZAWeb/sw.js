// Minimal app-shell service worker: caches the static app shell on
// install, then network-first for everything (fast reload from cache
// if the network is slow/unavailable, but never serves stale JS/CSS
// over a working connection). Bump CACHE_NAME on every deploy so old
// clients pick up the new shell instead of getting stuck on a cached
// one.
const CACHE_NAME = "chuza-web-v1";
const APP_SHELL = [
  "./",
  "index.html",
  "manifest.json",
  "css/style.css",
  "js/app.js",
  "js/net.js",
  "js/config.js",
  "js/schema.js",
  "js/lib/mqtt.min.js",
  "icons/apple-touch-icon.png",
  "icons/icon-192.png",
  "icons/icon-512.png",
];

self.addEventListener("install", (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => cache.addAll(APP_SHELL)).then(() => self.skipWaiting())
  );
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches
      .keys()
      .then((keys) => Promise.all(keys.filter((k) => k !== CACHE_NAME).map((k) => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener("fetch", (event) => {
  if (event.request.method !== "GET") return;
  event.respondWith(
    fetch(event.request)
      .then((response) => {
        const clone = response.clone();
        caches.open(CACHE_NAME).then((cache) => cache.put(event.request, clone));
        return response;
      })
      .catch(() => caches.match(event.request))
  );
});
