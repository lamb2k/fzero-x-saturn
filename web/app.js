/* Native/shared-core preview. MIT; see ../LICENSE. */
'use strict';

(() => {
  const $ = id => document.getElementById(id);
  const canvas = $('game');
  const context = canvas.getContext('2d', { alpha: false });
  const keys = new Set();
  const pointers = new Map();
  const buttons = { ArrowLeft: 1, KeyA: 1, ArrowRight: 2, KeyD: 2,
    ArrowUp: 4, KeyW: 4, ArrowDown: 8, KeyS: 8 };
  const fixedStep = 1000 / 60;
  let api, memory, ready = false, playing = false, started = false;
  let lastTime = 0, accumulator = 0, request = 0;
  let imported = false, courseTitle = 'Test circuit';

  function feedback(message, error = false) {
    $('feedback').textContent = message;
    $('feedback').classList.toggle('error', error);
  }

  function releaseControls() {
    keys.clear();
    pointers.clear();
    document.querySelectorAll('[data-control]').forEach(button => {
      button.classList.remove('pressed');
      button.setAttribute('aria-pressed', 'false');
    });
  }

  function updateRunControls() {
    $('play').textContent = playing ? 'Pause' : started ? 'Resume' : 'Start driving';
    $('start').textContent = started ? 'Resume driving →' : 'Start driving →';
    $('screen-overlay').hidden = playing;
    $('overlay-caption').textContent = started ? 'Paused' : 'Test circuit · Prototype handling';
    if (!started && imported) $('overlay-caption').textContent = 'Imported course · Prototype handling';
    $('play-state').textContent = playing ? 'Driving' : started ? 'Paused' : 'Ready';
    $('play-state').classList.toggle('live', playing);
  }

  function pause() {
    playing = false;
    accumulator = 0;
    releaseControls();
    if (request) cancelAnimationFrame(request);
    request = 0;
    if (ready) updateRunControls();
  }

  function start() {
    if (!ready || playing) return;
    feedback('');
    playing = true;
    started = true;
    lastTime = performance.now();
    accumulator = 0;
    updateRunControls();
    canvas.focus({ preventScroll: true });
    request = requestAnimationFrame(animate);
  }

  function reset() {
    if (!ready) return;
    pause();
    api.fzx_step(16);
    started = false;
    render();
    updateRunControls();
    feedback('Run reset.');
  }

  function inputMask() {
    let mask = 0;
    for (const key of keys) mask |= buttons[key] || 0;
    for (const value of pointers.values()) mask |= value;
    return mask;
  }

  function paintBackground() {
    const sky = context.createLinearGradient(0, 0, 0, 224);
    sky.addColorStop(0, '#08131f');
    sky.addColorStop(0.54, '#1d304b');
    sky.addColorStop(0.55, '#131c2c');
    sky.addColorStop(1, '#080f18');
    context.fillStyle = sky;
    context.fillRect(0, 0, 320, 224);
    context.fillStyle = '#738aa3';
    for (const [x, y] of [[19, 31], [54, 16], [91, 41], [142, 22], [172, 53], [221, 18], [262, 36], [301, 12]]) {
      context.fillRect(x, y, 1, 1);
    }
  }

  function render() {
    if (!ready) return;
    const pointer = api.fzx_render() >>> 0;
    if (pointer > memory.buffer.byteLength - 16) throw new Error('Invalid frame address');
    const data = new DataView(memory.buffer);
    const count = data.getUint32(pointer, true);
    if (count > 512 || pointer + 16 + count * 24 > memory.buffer.byteLength) {
      throw new Error('Invalid polygon count');
    }
    paintBackground();
    for (let i = 0; i < count; ++i) {
      const base = pointer + 16 + i * 24;
      const color = data.getUint16(base + 16, true);
      const red = Math.round((color & 31) * 255 / 31);
      const green = Math.round(((color >>> 5) & 31) * 255 / 31);
      const blue = Math.round(((color >>> 10) & 31) * 255 / 31);
      context.fillStyle = `rgb(${red},${green},${blue})`;
      context.beginPath();
      context.moveTo(data.getInt16(base, true), data.getInt16(base + 8, true));
      for (let vertex = 1; vertex < 4; ++vertex) {
        context.lineTo(data.getInt16(base + vertex * 2, true), data.getInt16(base + 8 + vertex * 2, true));
      }
      context.closePath();
      context.fill();
    }
    $('speed').textContent = String(data.getUint32(pointer + 4, true));
    $('lap').textContent = String(data.getUint32(pointer + 8, true));
    $('polygons').textContent = String(count);
  }

  function fail(error) {
    pause();
    ready = false;
    $('failure').hidden = false;
    $('failure').textContent = 'The preview could not run. See the source repository for build instructions.';
    $('play-state').textContent = 'Unavailable';
    $('overlay-caption').textContent = 'Preview unavailable';
    document.querySelectorAll('button, select, input').forEach(control => { control.disabled = true; });
    console.error(error);
  }

  function animate(now) {
    request = 0;
    if (!playing) return;
    try {
      // Never simulate a tab suspension as a huge catch-up burst.
      accumulator += Math.min(Math.max(now - lastTime, 0), 100);
      lastTime = now;
      while (accumulator >= fixedStep) {
        api.fzx_step(inputMask());
        accumulator -= fixedStep;
      }
      render();
      request = requestAnimationFrame(animate);
    } catch (error) { fail(error); }
  }

  function int(value, min, max, label) {
    if (!Number.isSafeInteger(value) || value < min || value > max) {
      throw new Error(`${label} is outside the supported range.`);
    }
    return value;
  }

  function validateCourse(value) {
    if (!value || typeof value !== 'object' || value.schema_version !== 1 || value.coordinate_scale !== 16) {
      throw new Error('Use a version 1 course JSON from this project’s importer.');
    }
    const count = int(value.count, 4, 64, 'Control point count');
    if (!Array.isArray(value.points) || value.points.length !== count) {
      throw new Error('Control point count does not match the course.');
    }
    const points = value.points.map((point, index) => {
      const label = `Point ${index + 1}`;
      if (!point || typeof point !== 'object' || !point.position || typeof point.position !== 'object') {
        throw new Error(`${label} is missing its position.`);
      }
      return {
        x: int(point.position.x, -2147483648, 2147483647, `${label} X`),
        y: int(point.position.y, -2147483648, 2147483647, `${label} Y`),
        z: int(point.position.z, -2147483648, 2147483647, `${label} Z`),
        left: int(point.radius_left, 1, 2147483647, `${label} left radius`),
        right: int(point.radius_right, 1, 2147483647, `${label} right radius`),
        bank: int(point.bank, 0, 65535, `${label} bank`),
        flags: int(point.track_segment_info, 0, 4294967295, `${label} track flags`),
        tension: int(point.tension, 0, 131072, `${label} tension`),
      };
    });
    return { count, points };
  }

  async function loadCourse(file) {
    if (!ready || !file) return;
    pause();
    try {
      if (file.size > 262144) throw new Error('Course JSON must be smaller than 256 KiB.');
      const source = JSON.parse(await file.text());
      const parsed = validateCourse(source);
      const pointer = api.fzx_course_buffer() >>> 0;
      if (pointer > memory.buffer.byteLength - 2052) throw new Error('Course buffer is unavailable.');
      const data = new DataView(memory.buffer);
      new Uint8Array(memory.buffer, pointer, 2052).fill(0);
      data.setUint32(pointer, parsed.count, true);
      parsed.points.forEach((point, index) => {
        const base = pointer + 4 + index * 32;
        data.setInt32(base, point.x, true);
        data.setInt32(base + 4, point.y, true);
        data.setInt32(base + 8, point.z, true);
        data.setInt32(base + 12, point.left, true);
        data.setInt32(base + 16, point.right, true);
        data.setUint16(base + 20, point.bank, true);
        data.setUint32(base + 24, point.flags, true);
        data.setInt32(base + 28, point.tension, true);
      });
      if (!api.fzx_activate_course()) throw new Error('This course is outside the native prototype’s supported geometry.');
      imported = true;
      started = false;
      const title = source.course_name || source.name || source.provenance?.course_name || file.name.replace(/\.json$/i, '');
      courseTitle = typeof title === 'string' ? title.slice(0, 80) : 'Imported course';
      $('course-name').textContent = courseTitle;
      $('course-kind').textContent = 'Imported course · Prototype handling';
      updateRunControls();
      render();
      feedback(`Loaded ${parsed.count} control points locally.`);
    } catch (error) {
      feedback(error instanceof SyntaxError ? 'That file is not valid course JSON.' : error.message, true);
    } finally { $('course-file').value = ''; }
  }

  $('start').addEventListener('click', start);
  $('play').addEventListener('click', () => playing ? pause() : start());
  $('reset').addEventListener('click', reset);
  $('course-file').addEventListener('click', pause);
  $('course-file').addEventListener('change', event => { void loadCourse(event.target.files[0]); });
  $('use-demo').addEventListener('click', () => {
    if (!ready) return;
    pause();
    api.fzx_use_demo();
    imported = false;
    started = false;
    courseTitle = 'Test circuit';
    $('course-name').textContent = courseTitle;
    $('course-kind').textContent = 'Prototype handling';
    updateRunControls();
    render();
    feedback('Test circuit loaded.');
  });
  $('transform-mode').addEventListener('change', event => {
    if (!ready) return;
    api.fzx_set_transform_mode(Number(event.target.value));
    render();
  });

  document.addEventListener('keydown', event => {
    if (!ready || /^(INPUT|SELECT|TEXTAREA|BUTTON)$/.test(event.target.tagName)) return;
    if (event.code === 'Escape') { pause(); return; }
    if (event.code === 'KeyR' && (playing || event.target === canvas)) {
      event.preventDefault(); reset(); return;
    }
    if (event.code === 'Space' && event.target === canvas && !event.repeat) {
      event.preventDefault(); playing ? pause() : start(); return;
    }
    if (buttons[event.code] && playing) {
      event.preventDefault();
      keys.add(event.code);
    }
  });
  document.addEventListener('keyup', event => { keys.delete(event.code); });
  window.addEventListener('blur', pause);
  document.addEventListener('visibilitychange', () => { if (document.hidden) pause(); });
  canvas.addEventListener('blur', event => {
    // Touch buttons retain control of the run; settings and links pause it.
    if (playing && !(event.relatedTarget && event.relatedTarget.matches('[data-control], #play, #reset'))) pause();
  });
  document.querySelectorAll('[data-control]').forEach(button => {
    button.setAttribute('aria-pressed', 'false');
    button.addEventListener('pointerdown', event => {
      if (!ready) return;
      event.preventDefault();
      if (!playing) start();
      button.setPointerCapture(event.pointerId);
      pointers.set(event.pointerId, Number(button.dataset.control));
      button.classList.add('pressed');
      button.setAttribute('aria-pressed', 'true');
    });
    const release = event => {
      pointers.delete(event.pointerId);
      const stillPressed = Array.from(pointers.values()).includes(Number(button.dataset.control));
      button.classList.toggle('pressed', stillPressed);
      button.setAttribute('aria-pressed', String(stillPressed));
    };
    button.addEventListener('pointerup', release);
    button.addEventListener('pointercancel', release);
    button.addEventListener('lostpointercapture', release);
  });

  async function initialize() {
    try {
      let bytes;
      if (typeof window.FZX_WASM_BASE64 === 'string') {
        bytes = Uint8Array.from(atob(window.FZX_WASM_BASE64), character => character.charCodeAt(0));
      } else {
        const response = await fetch('fzero.wasm');
        if (!response.ok) throw new Error(`WebAssembly build unavailable (${response.status})`);
        bytes = await response.arrayBuffer();
      }
      const result = await WebAssembly.instantiate(bytes, {});
      api = result.instance.exports;
      memory = api.memory;
      for (const name of ['fzx_init', 'fzx_step', 'fzx_render', 'fzx_course_buffer', 'fzx_activate_course', 'fzx_use_demo', 'fzx_set_transform_mode']) {
        if (typeof api[name] !== 'function') throw new Error(`Missing shared-core export: ${name}`);
      }
      api.fzx_init();
      api.fzx_set_transform_mode(1);
      ready = true;
      document.querySelectorAll('button, select, input').forEach(control => { control.disabled = false; });
      updateRunControls();
      render();
      document.documentElement.dataset.ready = 'true';
    } catch (error) { fail(error); }
  }
  paintBackground();
  void initialize();
})();
