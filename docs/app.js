// Live release info + tab switching + copy buttons. Static fallback if API fails.
(() => {
  const REPO = "Hushayo/PretClient";

  document.querySelectorAll(".tab").forEach((t) => {
    t.addEventListener("click", () => {
      document.querySelectorAll(".tab").forEach((x) => x.classList.remove("active"));
      t.classList.add("active");
      document.getElementById("term-direct").classList.toggle("hidden", t.dataset.tab !== "direct");
      document.getElementById("term-winget").classList.toggle("hidden", t.dataset.tab !== "winget");
    });
  });

  const copyBtn = document.getElementById("copy-btn");
  if (copyBtn) {
    copyBtn.addEventListener("click", async () => {
      const active = document.querySelector(".tab.active")?.dataset.tab === "winget"
        ? document.getElementById("term-winget")
        : document.getElementById("term-direct");
      try {
        await navigator.clipboard.writeText(active.innerText);
        copyBtn.textContent = "Copied";
        setTimeout(() => (copyBtn.textContent = "Copy"), 1200);
      } catch { /* clipboard unavailable */ }
    });
  }

  const hashBtn = document.getElementById("hash-btn");
  if (hashBtn) {
    hashBtn.addEventListener("click", async () => {
      const sha = document.getElementById("sha-value")?.textContent.trim();
      if (!sha) return;
      try {
        await navigator.clipboard.writeText(sha);
        hashBtn.textContent = "Copied";
        setTimeout(() => (hashBtn.textContent = "Copy SHA256"), 1200);
      } catch { /* noop */ }
    });
  }

  (async () => {
    try {
      const res = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, {
        headers: { Accept: "application/vnd.github+json" },
      });
      if (!res.ok) return;
      const rel = await res.json();
      if (!rel.tag_name) return;
      const tag = rel.tag_name;
      const ver = tag.replace(/^v/, "");
      const badge = document.getElementById("version-badge");
      const latest = document.getElementById("latest-version");
      const meta = document.getElementById("release-meta");
      if (badge) badge.textContent = tag;
      if (latest) latest.textContent = tag;
      const asset = (rel.assets || []).find((a) => /PretClient-Setup\.exe$/i.test(a.name));
      const url = asset?.browser_download_url
        || `https://github.com/${REPO}/releases/download/${tag}/PretClient-Setup.exe`;
      for (const id of ["download-btn", "download-btn2"]) {
        const el = document.getElementById(id);
        if (el) el.href = url;
      }
      if (meta && rel.published_at) {
        const d = new Date(rel.published_at).toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" });
        meta.innerHTML = `Windows 10/11 x64 &middot; ${tag} released ${d} &middot; Free &amp; open source (GPL-3.0)`;
      }
      // Refresh code snippets + hash if the API knows more than the hardcoded fallback.
      const digest = (asset?.digest || "").replace(/^sha256:/i, "");
      if (digest && document.getElementById("sha-value")) {
        document.getElementById("sha-value").textContent = digest;
      }
      const direct = document.querySelector("#term-direct code");
      if (direct) {
        direct.textContent =
`# Option 1 — direct (works today)\nInvoke-WebRequest -OutFile PretClient-Setup.exe \`\n  https://github.com/${REPO}/releases/download/${tag}/PretClient-Setup.exe\n.\\PretClient-Setup.exe`;
      }
      const wing = document.querySelector("#term-winget code");
      if (wing) {
        wing.textContent =
`# Option 2 — winget, official ID (after first winget-pkgs merge)\nwinget install Hushayo.PretClient\n\n# Until then — install from this repo's manifests (works today)\ngit clone https://github.com/${REPO}.git\nwinget install --manifest .\\PretClient\\winget\\manifests\\h\\hushayo\\pretclient\\${ver}\\Hushayo.PretClient.yaml`;
      }
    } catch { /* static fallback stays */ }
  })();
})();
