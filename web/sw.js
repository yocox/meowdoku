"use strict";

const APP_CACHE = "meowdoku-app-v1";
const LEVEL_CACHE = "meowdoku-levels-v1";
const META_CACHE = "meowdoku-meta-v1";
const APP_CACHE_PREFIX = "meowdoku-app-";
const MANIFEST_PATH = "levels/bundle/manifest.json";
const APP_SHELL = ["./", "./index.html", "./styles.css", "./game.js"];
const BUNDLE_CONCURRENCY = 3;

const scopeUrl = new URL(self.registration.scope);
const manifestUrl = new URL(MANIFEST_PATH, scopeUrl);
const bundleBaseUrl = new URL("levels/bundle/", scopeUrl);
let syncPromise = null;

self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(APP_CACHE).then((cache) => cache.addAll(APP_SHELL)));
});

self.addEventListener("activate", (event) => {
  event.waitUntil((async () => {
    const names = await caches.keys();
    await Promise.all(names
      .filter((name) => name.startsWith(APP_CACHE_PREFIX) && name !== APP_CACHE)
      .map((name) => caches.delete(name)));
    await self.clients.claim();
  })());
});

self.addEventListener("fetch", (event) => {
  const request = event.request;
  if (request.method !== "GET") return;

  const url = new URL(request.url);
  if (url.origin !== scopeUrl.origin) return;

  if (url.pathname === manifestUrl.pathname) {
    event.respondWith(fetchManifest(request));
    return;
  }
  if (url.pathname.startsWith(bundleBaseUrl.pathname) && url.pathname.endsWith(".json")) {
    event.respondWith(cacheFirstBundle(request));
    return;
  }
  if (request.mode === "navigate" || ["index.html", "styles.css", "game.js"].some((name) => url.pathname.endsWith(`/${name}`))) {
    event.respondWith(networkFirstApp(request));
  }
});

self.addEventListener("message", (event) => {
  if (event.data?.type === "SYNC_LEVEL_BUNDLES") {
    event.waitUntil(startBundleSync(event.data.manifest));
  } else if (event.data?.type === "CLEAR_CACHES") {
    const port = event.ports[0];
    event.waitUntil(clearMeowdokuCaches()
      .then(() => port?.postMessage({ ok: true }))
      .catch((error) => port?.postMessage({ ok: false, error: error.message })));
  }
});

async function clearMeowdokuCaches() {
  if (syncPromise) {
    try { await syncPromise; } catch { }
  }
  const names = await caches.keys();
  await Promise.all(names.filter((name) => name.startsWith("meowdoku-")).map((name) => caches.delete(name)));
}

async function networkFirstApp(request) {
  const cache = await caches.open(APP_CACHE);
  try {
    const response = await fetch(request, { cache: "no-store" });
    if (response.ok) await cache.put(request, response.clone());
    return response;
  } catch (error) {
    const cached = await cache.match(request);
    if (cached) return cached;
    if (request.mode === "navigate") {
      const fallback = await cache.match(new URL("index.html", scopeUrl));
      if (fallback) return fallback;
    }
    throw error;
  }
}

async function fetchManifest(request) {
  try {
    const response = await fetch(request, { cache: "no-store" });
    if (!response.ok) throw new Error(`manifest request failed: ${response.status}`);
    return response;
  } catch (error) {
    const cache = await caches.open(META_CACHE);
    const cached = await cache.match(manifestUrl.href);
    if (cached) return cached;
    throw error;
  }
}

async function cacheFirstBundle(request) {
  const cache = await caches.open(LEVEL_CACHE);
  const cached = await cache.match(request);
  if (cached) return cached;
  const response = await fetch(request, { cache: "no-store" });
  if (response.ok) await cache.put(request, response.clone());
  return response;
}

function startBundleSync(manifest) {
  if (!syncPromise) {
    syncPromise = syncLevelBundles(manifest).finally(() => { syncPromise = null; });
  }
  return syncPromise;
}

function validateManifest(manifest) {
  if (!manifest || manifest.schemaVersion !== 1 || !manifest.packs || Array.isArray(manifest.packs)) {
    throw new Error("invalid level manifest");
  }
  const entries = Object.entries(manifest.packs);
  if (entries.length === 0) throw new Error("level manifest has no packs");
  for (const [pack, entry] of entries) {
    if (!/^[a-z0-9]+$/.test(pack)
      || !entry || !Number.isInteger(entry.count) || entry.count < 1
      || entry.file !== `${pack}.json`
      || !/^[0-9a-f]{64}$/.test(entry.sha256)) {
      throw new Error(`invalid manifest entry: ${pack}`);
    }
  }
  return entries;
}

async function syncLevelBundles(manifest) {
  const entries = validateManifest(manifest);
  const cache = await caches.open(LEVEL_CACHE);
  let next = 0;

  async function worker() {
    while (next < entries.length) {
      const [pack, entry] = entries[next++];
      await ensureBundle(cache, pack, entry);
    }
  }

  await Promise.all(Array.from({ length: Math.min(BUNDLE_CONCURRENCY, entries.length) }, worker));

  const manifestBody = JSON.stringify(manifest);
  const metaCache = await caches.open(META_CACHE);
  await metaCache.put(manifestUrl.href, new Response(manifestBody, {
    headers: { "Content-Type": "application/json; charset=utf-8" },
  }));

  const wanted = new Set(entries.map(([pack, entry]) => bundleVersionUrl(pack, entry)));
  const requests = await cache.keys();
  await Promise.all(requests.filter((request) => !wanted.has(request.url)).map((request) => cache.delete(request)));
}

function bundleVersionUrl(pack, entry) {
  const url = new URL(entry.file, bundleBaseUrl);
  url.searchParams.set("v", entry.sha256);
  return url.href;
}

async function ensureBundle(cache, pack, entry) {
  const versionUrl = bundleVersionUrl(pack, entry);
  const cached = await cache.match(versionUrl);
  if (cached) {
    try {
      await validateBundleResponse(cached, pack, entry);
      return;
    } catch {
      await cache.delete(versionUrl);
    }
  }

  const response = await fetch(versionUrl, { cache: "no-store" });
  if (!response.ok) throw new Error(`bundle request failed: ${pack} (${response.status})`);
  const bytes = await validateBundleResponse(response, pack, entry);
  await cache.put(versionUrl, new Response(bytes, {
    headers: { "Content-Type": "application/json; charset=utf-8" },
  }));
}

async function validateBundleResponse(response, pack, entry) {
  const bytes = await response.arrayBuffer();
  const actualHash = await sha256Hex(bytes);
  if (actualHash !== entry.sha256) throw new Error(`bundle hash mismatch: ${pack}`);

  let bundle;
  try {
    bundle = JSON.parse(new TextDecoder().decode(bytes));
  } catch {
    throw new Error(`invalid bundle JSON: ${pack}`);
  }
  if (bundle.schemaVersion !== 1 || bundle.pack !== pack
    || !Array.isArray(bundle.levels) || bundle.levels.length !== entry.count
    || bundle.levels.some((level) => typeof level !== "string")) {
    throw new Error(`invalid bundle content: ${pack}`);
  }
  return bytes;
}

async function sha256Hex(bytes) {
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(digest)].map((value) => value.toString(16).padStart(2, "0")).join("");
}
