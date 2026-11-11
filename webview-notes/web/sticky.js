(() => {
  const $ = (sel) => document.querySelector(sel);
  const noteId = (location.hash || "").replace(/^#/, "") ||
    new URLSearchParams(location.search).get("id");

  const state = {
    note: null,
    saveTimer: null,
    dirty: false,
  };

  const ui = {
    title: $("#title-label"),
    save: $("#save-state"),
    text: $("#text-editor"),
    check: $("#check-editor"),
    rows: $("#check-rows"),
    btnBold: $("#btn-bold"),
    btnCheck: $("#btn-check"),
    btnAddRow: $("#btn-add-row"),
    btnMin: $("#btn-min"),
    btnClose: $("#btn-close"),
  };

  function api() {
    return window.pywebview?.api;
  }

  function pick(fnNames) {
    const a = api();
    if (!a) return null;
    for (const n of fnNames) {
      if (typeof a[n] === "function") return a[n].bind(a);
    }
    return null;
  }

  async function waitApi() {
    for (let i = 0; i < 120; i++) {
      // pywebview.api is {} before methods are injected — wait for a real method.
      if (pick(["get_note", "getNote"])) return api();
      await new Promise((r) => setTimeout(r, 50));
    }
    throw new Error("pywebview api not ready");
  }

  function tagsFromBody(body) {
    const first = body.split("\n", 1)[0] || "";
    const tags = [];
    let i = 0;
    while (i < first.length) {
      while (i < first.length && /\s/.test(first[i])) i++;
      if (i >= first.length || first[i] !== "#") break;
      i++;
      const start = i;
      while (i < first.length && !/\s/.test(first[i]) && first[i] !== "#") i++;
      if (i > start) {
        const t = first.slice(start, i).toLowerCase();
        if (!tags.includes(t)) tags.push(t);
      }
    }
    return tags;
  }

  function parseChecklist(body) {
    const lines = body.split(/\r?\n/);
    const items = [];
    for (const line of lines) {
      let m = line.match(/^- \[([xX ])\]\s?(.*)$/);
      if (m) {
        items.push({ done: m[1].toLowerCase() === "x", text: m[2] });
        continue;
      }
      m = line.match(/^- \[\]\s?(.*)$/);
      if (m) {
        items.push({ done: false, text: m[1] });
        continue;
      }
      if (line.trim() || items.length === 0) items.push({ done: false, text: line });
    }
    if (!items.length) items.push({ done: false, text: "" });
    return items;
  }

  function checklistBody() {
    return [...ui.rows.querySelectorAll(".check-row")]
      .map((row) => {
        const done = row.querySelector('input[type="checkbox"]').checked;
        const text = row.querySelector('input[type="text"]').value;
        return `- [${done ? "x" : " "}] ${text}`;
      })
      .join("\n");
  }

  function setSaveState(text) {
    ui.save.textContent = text;
  }

  function markDirty() {
    state.dirty = true;
    setSaveState("Saving…");
    clearTimeout(state.saveTimer);
    state.saveTimer = setTimeout(() => save(), 400);
  }

  async function save() {
    if (!state.note) return;
    const body = state.note.mode === "check" ? checklistBody() : ui.text.value;
    state.note.body = body;
    state.note.tags = tagsFromBody(body);
    const saveNote = pick(["save_note", "saveNote"]);
    if (!saveNote) return;
    try {
      await saveNote({
        id: state.note.id,
        body,
        mode: state.note.mode,
        hidden: false,
        x: state.note.x,
        y: state.note.y,
        w: state.note.w,
        h: state.note.h,
      });
      state.dirty = false;
      setSaveState("Saved");
      updateTitle();
    } catch (err) {
      setSaveState("Error");
      console.error(err);
    }
  }

  function updateTitle() {
    const note = state.note;
    if (!note) return;
    if (note.tags?.length) {
      ui.title.textContent = `#${note.tags[0]}${note.tags.length > 1 ? " …" : ""}`;
    } else {
      ui.title.textContent = note.mode === "check" ? "Checklist" : "Sticky";
    }
  }

  function showEditors(mode) {
    ui.text.hidden = mode !== "text";
    ui.check.hidden = mode !== "check";
    ui.btnBold.disabled = mode !== "text";
  }

  function renderChecklist(items) {
    ui.rows.innerHTML = "";
    for (const item of items) addCheckRow(item, false);
  }

  function addCheckRow(item, focus = false, afterRow = null) {
    const row = document.createElement("div");
    row.className = "check-row" + (item.done ? " done" : "");
    const cb = document.createElement("input");
    cb.type = "checkbox";
    cb.checked = !!item.done;
    const input = document.createElement("input");
    input.type = "text";
    input.value = item.text || "";
    cb.onchange = () => {
      row.classList.toggle("done", cb.checked);
      markDirty();
    };
    input.oninput = markDirty;
    input.onkeydown = (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        addCheckRow({ done: false, text: "" }, true, row);
        markDirty();
      } else if (e.key === "Backspace" && input.value === "" && ui.rows.children.length > 1) {
        e.preventDefault();
        const prev = row.previousElementSibling;
        row.remove();
        if (prev) {
          const t = prev.querySelector('input[type="text"]');
          t.focus();
          t.setSelectionRange(t.value.length, t.value.length);
        }
        markDirty();
      }
    };
    row.append(cb, input);
    if (afterRow) afterRow.after(row);
    else ui.rows.appendChild(row);
    if (focus) input.focus();
  }

  function toChecklistItems(value) {
    const cleaned = value.replace(/\[\]/g, "");
    const items = cleaned.split(/\r?\n/).map((line) => {
      let t = line;
      if (t.startsWith("- ")) t = t.slice(2);
      else {
        const num = t.match(/^\d+\.\s+(.*)$/);
        if (num) t = num[1];
      }
      return { done: false, text: t.replace(/^\s+/, "") };
    });
    while (items.length > 1 && !items[items.length - 1].text) items.pop();
    if (!items.length) items.push({ done: false, text: "" });
    return items;
  }

  function applyChecklist(items) {
    state.note.mode = "check";
    state.note.body = items.map((it) => `- [${it.done ? "x" : " "}] ${it.text}`).join("\n");
    renderChecklist(items);
    showEditors("check");
    updateTitle();
    markDirty();
    ui.rows.lastElementChild?.querySelector('input[type="text"]')?.focus();
  }

  function wrapBold() {
    const el = ui.text;
    const start = el.selectionStart;
    const end = el.selectionEnd;
    const val = el.value;
    const selected = val.slice(start, end);
    el.value = val.slice(0, start) + `**${selected}**` + val.slice(end);
    el.setSelectionRange(selected ? start + 2 : start + 2, selected ? end + 2 : start + 2);
    markDirty();
  }

  function toggleMode() {
    if (state.note.mode === "text") {
      applyChecklist(toChecklistItems(ui.text.value || ""));
    } else {
      state.note.body = checklistBody()
        .split(/\r?\n/)
        .map((ln) => ln.replace(/^- \[[xX ]\]\s?/, "").replace(/^- \[\]\s?/, ""))
        .join("\n");
      state.note.mode = "text";
      ui.text.value = state.note.body;
      showEditors("text");
      markDirty();
      ui.text.focus();
    }
    updateTitle();
  }

  function loadNote(note) {
    state.note = note;
    if (note.mode === "check") {
      renderChecklist(parseChecklist(note.body || ""));
      showEditors("check");
    } else {
      ui.text.value = note.body || "";
      showEditors("text");
      ui.text.focus();
    }
    updateTitle();
  }

  function bind() {
    ui.btnBold.onclick = () => wrapBold();
    ui.btnCheck.onclick = () => toggleMode();
    ui.btnAddRow.onclick = () => {
      addCheckRow({ done: false, text: "" }, true);
      markDirty();
    };
    ui.btnMin.onclick = async (e) => {
      e.preventDefault();
      e.stopPropagation();
      if (state.dirty) await save();
      const fn = pick(["minimize_note", "minimizeNote"]);
      // fire-and-forget — don't await window ops
      fn?.(noteId);
    };
    ui.btnClose.onclick = async (e) => {
      e.preventDefault();
      e.stopPropagation();
      if (state.dirty) await save();
      const fn = pick(["close_note", "closeNote"]);
      fn?.(noteId);
    };

    ui.text.oninput = () => {
      if ((ui.text.value || "").includes("[]")) {
        applyChecklist(toChecklistItems(ui.text.value));
        return;
      }
      state.note.body = ui.text.value;
      state.note.tags = tagsFromBody(state.note.body);
      markDirty();
    };

    document.addEventListener("keydown", (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "b" && !ui.text.hidden) {
        e.preventDefault();
        wrapBold();
      }
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") {
        e.preventDefault();
        save();
      }
    });
  }

  async function boot() {
    if (!noteId) throw new Error("missing note id");
    bind();
    await waitApi();
    const getNote = pick(["get_note", "getNote"]);
    const note = await getNote(noteId);
    if (!note) throw new Error("note not found: " + noteId);
    loadNote(note);
  }

  boot().catch((err) => {
    ui.title.textContent = String(err);
    console.error(err);
  });
})();
