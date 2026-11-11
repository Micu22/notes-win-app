(() => {
  const $ = (sel) => document.querySelector(sel);

  const state = {
    notes: [],
    filter: "",
    selectedId: null,
  };

  const ui = {
    filters: $("#filters"),
    list: $("#note-list"),
    btnNew: $("#btn-new"),
    btnOpen: $("#btn-open"),
    btnDelete: $("#btn-delete"),
  };

  function api() {
    return window.pywebview?.api;
  }

  async function waitApi() {
    for (let i = 0; i < 120; i++) {
      const a = api();
      if (a && (typeof a.list_notes === "function" || typeof a.listNotes === "function")) {
        return a;
      }
      await new Promise((r) => setTimeout(r, 50));
    }
    throw new Error("pywebview api not ready");
  }

  function call(names, ...args) {
    const a = api();
    for (const n of names) {
      if (a && typeof a[n] === "function") return a[n](...args);
    }
    return Promise.reject(new Error("missing api: " + names.join("|")));
  }

  function allTags() {
    const set = new Set();
    for (const n of state.notes) for (const t of n.tags || []) set.add(t);
    return [...set].sort();
  }

  function selected() {
    return state.notes.find((n) => n.id === state.selectedId) || null;
  }

  function updateActions() {
    const has = !!selected();
    ui.btnOpen.disabled = !has;
    ui.btnDelete.disabled = !has;
  }

  function renderFilters() {
    const tags = allTags();
    ui.filters.innerHTML = "";
    const make = (label, value) => {
      const b = document.createElement("button");
      b.type = "button";
      b.className = "chip" + (state.filter === value ? " active" : "");
      b.textContent = label;
      b.onclick = () => {
        state.filter = value;
        renderFilters();
        renderList();
      };
      ui.filters.appendChild(b);
    };
    make("All", "");
    for (const t of tags) make(`#${t}`, t);
  }

  function renderList() {
    ui.list.innerHTML = "";
    for (const n of state.notes) {
      if (state.filter && !(n.tags || []).includes(state.filter)) continue;
      const li = document.createElement("li");
      if (n.id === state.selectedId) li.classList.add("active");
      const id = document.createElement("span");
      id.className = "id";
      let label = n.id;
      if (n.mode === "check") label += " ☑";
      if (n.open) label += " ●";
      else if (n.hidden) label += " (hidden)";
      id.textContent = label;
      const meta = document.createElement("span");
      meta.className = "meta";
      meta.textContent = (n.tags || []).map((t) => `#${t}`).join(" ") || "no tags";
      li.append(id, meta);
      li.onclick = () => {
        state.selectedId = n.id;
        renderList();
        updateActions();
      };
      li.ondblclick = () => openSelected();
      ui.list.appendChild(li);
    }
    updateActions();
  }

  async function refresh() {
    state.notes = await call(["list_notes", "listNotes"]);
    if (state.selectedId && !state.notes.some((n) => n.id === state.selectedId)) {
      state.selectedId = null;
    }
    renderFilters();
    renderList();
  }

  async function createNote() {
    const note = await call(["new_note", "newNote"]);
    state.selectedId = note.id;
    await refresh();
  }

  async function openSelected() {
    const n = selected();
    if (!n) return;
    await call(["open_note", "openNote"], n.id);
    await refresh();
  }

  async function deleteSelected() {
    const n = selected();
    if (!n) return;
    if (!confirm(`Delete ${n.id}?`)) return;
    await call(["delete_note", "deleteNote"], n.id);
    state.selectedId = null;
    await refresh();
  }

  function bind() {
    ui.btnNew.onclick = () => createNote();
    ui.btnOpen.onclick = () => openSelected();
    ui.btnDelete.onclick = () => deleteSelected();
    document.addEventListener("keydown", (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "n") {
        e.preventDefault();
        createNote();
      }
      if (e.key === "Enter" && selected()) {
        e.preventDefault();
        openSelected();
      }
    });
  }

  window.app = { refresh, createNote };

  boot();
  async function boot() {
    bind();
    await waitApi();
    await refresh();
  }
})();
