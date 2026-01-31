/* eslint-disable no-console */
(() => {
  // If you're hosting the front-end separately (GitHub Pages) and the back-end on Render,
  // set `window.API_BASE` in `index.html` to your Render URL, e.g.
  //   window.API_BASE = "https://your-service.onrender.com";
  // Leave it empty ("") for local dev when the C++ server serves both UI + API.
  const API_BASE = String(globalThis.API_BASE || "").replace(/\/+$/, "");

  const $count = document.getElementById("count");
  const $generate = document.getElementById("generate");
  const $copy = document.getElementById("copy");
  const $error = document.getElementById("error");
  const $list = document.getElementById("list");
  const $meta = document.getElementById("meta");

  async function fetchNames(count) {
    const qs = `count=${encodeURIComponent(String(count))}`;
    const url = API_BASE ? `${API_BASE}/api/generate?${qs}` : `/api/generate?${qs}`;

    const res = await fetch(url, {
      method: "GET",
      headers: { Accept: "application/json" },
    });

    const contentType = res.headers.get("content-type") || "";
    const isJson = contentType.includes("application/json");
    const data = isJson ? await res.json() : { error: await res.text() };

    if (!res.ok) {
      const msg = data?.error || `Request failed (${res.status})`;
      throw new Error(msg);
    }
    if (!data || !Array.isArray(data.names)) {
      throw new Error("Bad response from server.");
    }
    return data.names;
  }

  function setError(message) {
    $error.textContent = message || "";
  }

  function render(names) {
    $list.innerHTML = "";
    for (const name of names) {
      const li = document.createElement("li");
      li.textContent = name;
      $list.appendChild(li);
    }
  }

  async function copyAll(names) {
    const text = names.map((n, i) => `${i + 1}. ${n}`).join("\n");
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(text);
      return;
    }
    // Fallback for file:// or older browsers
    window.prompt("Copy the names:", text);
  }

  let lastNames = [];

  async function onGenerate() {
    setError("");
    $meta.textContent = "";

    const raw = String($count.value ?? "").trim();
    const count = Number(raw);

    if (!Number.isFinite(count) || !Number.isInteger(count)) {
      setError("Please enter a whole number.");
      return;
    }
    if (count <= 0) {
      setError("Count must be at least 1.");
      return;
    }
    if (count > 5000) {
      setError("Count is too large. Please use 5000 or less.");
      return;
    }

    $generate.disabled = true;
    $generate.textContent = "Generating…";
    $copy.disabled = true;
    try {
      lastNames = await fetchNames(count);
      render(lastNames);
      $meta.textContent = `${lastNames.length} generated`;
      $copy.disabled = lastNames.length === 0;
    } catch (e) {
      console.error(e);
      setError(e?.message || "Could not generate names.");
    } finally {
      $generate.disabled = false;
      $generate.textContent = "Generate";
    }
  }

  $generate.addEventListener("click", onGenerate);
  $copy.addEventListener("click", async () => {
    try {
      await copyAll(lastNames);
      setError("");
    } catch (e) {
      console.error(e);
      setError("Could not copy to clipboard in this browser context.");
    }
  });
  $count.addEventListener("keydown", (e) => {
    if (e.key === "Enter") onGenerate();
  });

  // Initial state (do not auto-generate)
  render([]);
  $meta.textContent = "";
  $copy.disabled = true;
})();

