// PretClient site: install tabs, copy buttons, live release info, and the demo launcher card.
// Everything has a working static fallback, so the page is correct even if this script or the API fails.
(() => {
  "use strict";

  const REPO = "Hushayo/PretClient";
  const $ = (sel, root = document) => root.querySelector(sel);
  const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

  /* ---------- install tabs (arrow keys, Home, End) ---------- */
  const tabs = $$('[role="tab"]');
  const selectTab = (tab) => {
    for (const t of tabs) {
      const on = t === tab;
      t.classList.toggle("active", on);
      t.setAttribute("aria-selected", String(on));
      if (on) t.removeAttribute("tabindex");
      else t.setAttribute("tabindex", "-1");
      const panel = document.getElementById(t.getAttribute("aria-controls"));
      if (panel) panel.hidden = !on;
    }
  };
  tabs.forEach((tab, i) => {
    tab.addEventListener("click", () => selectTab(tab));
    tab.addEventListener("keydown", (e) => {
      let next = null;
      if (e.key === "ArrowRight") next = tabs[(i + 1) % tabs.length];
      else if (e.key === "ArrowLeft") next = tabs[(i - 1 + tabs.length) % tabs.length];
      else if (e.key === "Home") next = tabs[0];
      else if (e.key === "End") next = tabs[tabs.length - 1];
      if (!next) return;
      e.preventDefault();
      selectTab(next);
      next.focus();
    });
  });

  /* ---------- copy buttons ---------- */
  const flash = (btn, text) => {
    if (!btn.dataset.idle) btn.dataset.idle = btn.textContent;
    btn.textContent = text;
    btn.classList.add("done");
    clearTimeout(btn._t);
    btn._t = setTimeout(() => {
      btn.textContent = btn.dataset.idle;
      btn.classList.remove("done");
    }, 1400);
  };
  $$("[data-copy]").forEach((btn) => {
    btn.addEventListener("click", async () => {
      // Selector is resolved at click time so the copy button follows the visible tab.
      const el = $(btn.dataset.copy);
      if (!el) return;
      try {
        await navigator.clipboard.writeText(el.innerText.trim());
        flash(btn, "Copied");
      } catch {
        flash(btn, "Copy failed");
      }
    });
  });

  /* ---------- live release info ---------- */
  const CACHE_KEY = "pretclient-release-v1";
  const CACHE_MS = 10 * 60 * 1000; // stay well under the unauthenticated API rate limit

  const loadRelease = async () => {
    try {
      const c = JSON.parse(sessionStorage.getItem(CACHE_KEY));
      if (c && Date.now() - c.t < CACHE_MS) return c.rel;
    } catch { /* no cache */ }
    const res = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, {
      headers: { Accept: "application/vnd.github+json" },
    });
    if (!res.ok) throw new Error(`GitHub API ${res.status}`);
    const r = await res.json();
    const rel = {
      tag_name: r.tag_name,
      published_at: r.published_at,
      assets: (r.assets || []).map((a) => ({ name: a.name, url: a.browser_download_url, digest: a.digest })),
    };
    try { sessionStorage.setItem(CACHE_KEY, JSON.stringify({ t: Date.now(), rel })); } catch { /* private mode */ }
    return rel;
  };

  const applyRelease = (rel) => {
    const tag = rel.tag_name;
    if (!tag) return;
    const ver = tag.replace(/^v/i, "");
    $$("[data-tag]").forEach((el) => (el.textContent = tag));
    $$("[data-ver]").forEach((el) => (el.textContent = ver));

    const badge = $("#version-badge");
    if (badge) { badge.textContent = tag; badge.hidden = false; }

    const asset = rel.assets.find((a) => /PretClient-Setup\.exe$/i.test(a.name));
    if (asset && asset.url) $$("[data-dl]").forEach((a) => (a.href = asset.url));

    const when = rel.published_at ? new Date(rel.published_at) : null;
    const dateEl = $("#release-date");
    if (dateEl && when && !isNaN(when)) {
      dateEl.textContent = `Latest is ${tag}, released ${when.toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" })}.`;
    }

    // Only show a checksum when it comes from the same release as the download link.
    const digest = ((asset && asset.digest) || "").replace(/^sha256:/i, "");
    if (/^[0-9a-f]{64}$/i.test(digest)) {
      $("#sha-value").textContent = digest;
      $("#hash-line").hidden = false;
    }
  };

  loadRelease().then(applyRelease).catch(() => { /* static fallback stays: /releases/latest/download */ });

  /* ---------- demo launcher card ---------- */
  const LOG = {
    fabric: ["Setting user: Steve", "Loading 14 mods", "Reloading ResourceManager", "Sound engine started"],
    vanilla: ["Setting user: Steve", "Reloading ResourceManager: vanilla", "Sound engine started"],
    neoforge: ["Setting user: Steve", "Loading 22 mods", "Reloading ResourceManager", "Sound engine started"],
  };
  const pad = (n) => String(n).padStart(2, "0");
  const clock = () => {
    const d = new Date();
    return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
  };
  const LABEL = { idle: "Idle", starting: "Starting", running: "Running" };

  $$("[data-instance]").forEach((card) => {
    const status = $("[data-status]", card);
    const stats = $("[data-stats]", card);
    const log = $("[data-log]", card);
    const play = $("[data-play]", card);
    const stop = $("[data-stop]", card);
    const lines = LOG[card.dataset.loader] || LOG.vanilla;
    const ramMax = parseFloat(card.dataset.ram) || 1;

    let timers = [];
    let ticker = null;
    let t0 = 0;
    let pid = 0;

    const setState = (s) => {
      card.dataset.state = s;
      status.textContent = LABEL[s];
      play.disabled = s !== "idle";
      stop.disabled = s === "idle";
    };

    const addLine = (text, last) => {
      const row = document.createElement("p");
      const t = document.createElement("span");
      t.className = "t";
      t.textContent = `[${clock()}] `;
      row.append(t, document.createTextNode(text));
      if (last) row.className = "ok";
      log.append(row);
      while (log.children.length > 4) log.firstChild.remove();
    };

    const render = () => {
      const s = (Date.now() - t0) / 1000;
      const cpu = s < 10 ? 10 + 45 * Math.exp(-s / 4) + Math.random() * 4 : 8 + Math.random() * 8;
      const ram = ramMax * (1 - Math.exp(-s / 5)) + Math.random() * 0.03 + 0.1;
      const gpu = s < 4 ? 4 + Math.random() * 4 : 10 + Math.random() * 12;
      stats.textContent = `pid ${pid} | CPU ${cpu.toFixed(1)}% | RAM ${ram.toFixed(1)} GB | GPU ${gpu.toFixed(1)}%`;
    };

    const start = () => {
      // Claim the slot synchronously: a second click can never start a second instance.
      if (card.dataset.state !== "idle") return;
      setState("starting");
      pid = 1000 + Math.floor(Math.random() * 30000);
      stats.textContent = "Launching...";
      log.textContent = "";
      log.hidden = false;
      lines.forEach((text, i) => {
        timers.push(setTimeout(() => addLine(text, i === lines.length - 1), 700 + i * 1100));
      });
      timers.push(setTimeout(() => {
        setState("running");
        t0 = Date.now();
        render();
        ticker = setInterval(render, 1000);
      }, 1500));
      stop.focus(); // Play just became disabled, keep keyboard focus somewhere useful
    };

    const halt = () => {
      if (card.dataset.state === "idle") return;
      timers.forEach(clearTimeout);
      timers = [];
      clearInterval(ticker);
      ticker = null;
      log.hidden = true;
      log.textContent = "";
      stats.textContent = "Ready to play";
      setState("idle");
      play.focus();
    };

    play.addEventListener("click", start);
    stop.addEventListener("click", halt);
  });
})();
