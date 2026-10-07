// Fill in live release info from GitHub API (repo is public).
// Falls back gracefully to the hardcoded VERSION when offline / rate-limited.
(async () => {
  const REPO = "Hushayo/PretClient";
  const badge = document.getElementById("version-badge");
  const latest = document.getElementById("latest-version");
  const meta = document.getElementById("release-meta");
  try {
    const res = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, {
      headers: { Accept: "application/vnd.github+json" },
    });
    if (!res.ok) return;
    const rel = await res.json();
    if (!rel.tag_name) return;
    const tag = rel.tag_name;
    if (badge) badge.textContent = tag;
    if (latest) latest.textContent = tag;
    const dl = document.getElementById("download-btn");
    const asset = (rel.assets || []).find((a) => /PretClient-Setup\.exe$/i.test(a.name));
    if (dl && asset && asset.browser_download_url) dl.href = asset.browser_download_url;
    if (meta && rel.published_at) {
      const d = new Date(rel.published_at).toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" });
      meta.innerHTML = `Windows 10/11 x64 &middot; ${tag} released ${d} &middot; Free &amp; open source (GPL-3.0)`;
    }
  } catch {
    // static fallback stays
  }
})();
