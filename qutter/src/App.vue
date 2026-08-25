<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from "vue";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";

const mode = ref("idle"); // idle | shooting | capturing | preview
const shot = ref(null); // { width, height, data(base64 PNG) }
const canvasRef = ref(null);
const sel = ref(null); // 选区（CSS 像素）
const errMsg = ref("");
const longInfo = ref({ frames: 1, height: 0 }); // 截取中进度
const longResult = ref(null); // { width, height, frames, preview, cancelled }
const savedPath = ref("");
const menuHidden = ref(false); // 拖动中隐藏顶部操作栏

const listeners = [];
const handles = ["nw", "n", "ne", "w", "e", "sw", "s", "se"];
let dragging = false; // 拖拽新选区
let startPos = null;
let resizing = false; // 拖动手柄调整选区
let resizeDir = "";
let resizeStart = null;
let resizeOrig = null;

/* ---------- 选区与尺寸 ---------- */
const selStyle = computed(() => {
  if (!sel.value) return {};
  const { x, y, w, h } = sel.value;
  return { left: x + "px", top: y + "px", width: w + "px", height: h + "px" };
});

/* 截图是物理像素，窗口显示是 CSS 像素，换算比例 */
const scale = () => {
  const c = canvasRef.value;
  return c && shot.value ? shot.value.width / c.clientWidth : 1;
};

const sizeText = computed(() => {
  if (!sel.value) return "";
  const s = scale();
  return `${Math.round(sel.value.w * s)} × ${Math.round(sel.value.h * s)}`;
});

const canConfirm = computed(
  () => !!sel.value && sel.value.w >= 3 && sel.value.h >= 3,
);

const previewSrc = computed(() =>
  longResult.value ? "data:image/png;base64," + longResult.value.preview : "",
);

/* ---------- 普通截图 ---------- */
function onScreenshot(payload) {
  shot.value = payload;
  sel.value = null;
  errMsg.value = "";
  mode.value = "shooting";
  nextTick(drawShot);
}

function drawShot() {
  const c = canvasRef.value;
  if (!c || !shot.value) return;
  c.width = shot.value.width;
  c.height = shot.value.height;
  const ctx = c.getContext("2d");
  const img = new Image();
  img.onload = () => ctx.drawImage(img, 0, 0);
  img.onerror = () => (errMsg.value = "截图数据解析失败");
  img.src = "data:image/png;base64," + shot.value.data;
}

function onMouseDown(e) {
  if (mode.value !== "shooting") return;
  const x = e.clientX;
  const y = e.clientY;
  // 点击在现有选区内 → 保留选区，不重新框选
  if (
    sel.value &&
    x >= sel.value.x &&
    x <= sel.value.x + sel.value.w &&
    y >= sel.value.y &&
    y <= sel.value.y + sel.value.h
  ) {
    return;
  }
  dragging = true;
  menuHidden.value = true; // 拖动中隐藏操作栏
  startPos = { x, y };
  sel.value = { x, y, w: 0, h: 0 };
}

function onMouseMove(e) {
  if (resizing) {
    updateResize(e);
    return;
  }
  if (!dragging) return;
  const x = e.clientX;
  const y = e.clientY;
  sel.value = {
    x: Math.min(startPos.x, x),
    y: Math.min(startPos.y, y),
    w: Math.abs(x - startPos.x),
    h: Math.abs(y - startPos.y),
  };
}

function onMouseUp() {
  menuHidden.value = false; // 松开后恢复操作栏
  if (resizing) {
    resizing = false;
    resizeDir = "";
    resizeStart = null;
    resizeOrig = null;
    return;
  }
  dragging = false;
  if (sel.value && (sel.value.w < 3 || sel.value.h < 3)) {
    sel.value = null;
  }
}

/* ---------- 手柄调整选区大小 ---------- */
function startResize(e, dir) {
  if (mode.value !== "shooting" || !sel.value) return;
  resizing = true;
  menuHidden.value = true; // 拖动中隐藏操作栏
  resizeDir = dir;
  resizeStart = { x: e.clientX, y: e.clientY };
  resizeOrig = { ...sel.value };
  e.preventDefault();
}

function updateResize(e) {
  if (!resizeOrig) return;
  const c = canvasRef.value;
  const maxW = c.clientWidth;
  const maxH = c.clientHeight;
  const clamp = (v, lo, hi) => Math.min(Math.max(v, lo), hi);
  const mx = clamp(e.clientX, 0, maxW);
  const my = clamp(e.clientY, 0, maxH);
  const o = resizeOrig;
  let { x, y, w, h } = o;
  switch (resizeDir) {
    case "nw": {
      const nx = Math.min(mx, o.x + o.w);
      const ny = Math.min(my, o.y + o.h);
      x = nx;
      y = ny;
      w = o.x + o.w - nx;
      h = o.y + o.h - ny;
      break;
    }
    case "n": {
      const ny = Math.min(my, o.y + o.h);
      y = ny;
      h = o.y + o.h - ny;
      break;
    }
    case "ne": {
      const ny = Math.min(my, o.y + o.h);
      y = ny;
      h = o.y + o.h - ny;
      w = Math.max(mx - o.x, 0);
      break;
    }
    case "w": {
      const nx = Math.min(mx, o.x + o.w);
      x = nx;
      w = o.x + o.w - nx;
      break;
    }
    case "e":
      w = Math.max(mx - o.x, 0);
      break;
    case "sw": {
      const nx = Math.min(mx, o.x + o.w);
      x = nx;
      w = o.x + o.w - nx;
      h = Math.max(my - o.y, 0);
      break;
    }
    case "s":
      h = Math.max(my - o.y, 0);
      break;
    case "se":
      w = Math.max(mx - o.x, 0);
      h = Math.max(my - o.y, 0);
      break;
  }
  w = Math.max(w, 3);
  h = Math.max(h, 3);
  x = clamp(x, 0, maxW - w);
  y = clamp(y, 0, maxH - h);
  sel.value = { x, y, w, h };
}

async function confirmSelection() {
  if (!canConfirm.value) return;
  const c = canvasRef.value;
  const s = scale();
  const x = Math.max(0, Math.round(sel.value.x * s));
  const y = Math.max(0, Math.round(sel.value.y * s));
  const w = Math.min(shot.value.width - x, Math.round(sel.value.w * s));
  const h = Math.min(shot.value.height - y, Math.round(sel.value.h * s));
  if (w < 1 || h < 1) return;
  try {
    const blob = await cropToPng(c, x, y, w, h);
    const bytes = new Uint8Array(await blob.arrayBuffer());
    await invoke("copy_image", { png: Array.from(bytes) });
    reset();
  } catch (e) {
    errMsg.value = "复制失败: " + String(e);
  }
}

function cropToPng(src, x, y, w, h) {
  const data = src.getContext("2d").getImageData(x, y, w, h);
  const out = document.createElement("canvas");
  out.width = w;
  out.height = h;
  out.getContext("2d").putImageData(data, 0, 0);
  return new Promise((resolve) => out.toBlob(resolve, "image/png"));
}

function cancel() {
  reset();
}

async function reset() {
  mode.value = "idle";
  shot.value = null;
  sel.value = null;
  longResult.value = null;
  savedPath.value = "";
  dragging = false;
  try {
    await invoke("finish_capture");
  } catch (_) {
    /* 窗口可能已不可用，忽略 */
  }
}

function exitApp() {
  invoke("exit_app");
}

/* ---------- 长截图 ---------- */
async function startLong() {
  if (!canConfirm.value) return;
  const s = scale();
  const args = {
    x: Math.round(sel.value.x * s),
    y: Math.round(sel.value.y * s),
    w: Math.round(sel.value.w * s),
    h: Math.round(sel.value.h * s),
  };
  longResult.value = null;
  savedPath.value = "";
  errMsg.value = "";
  longInfo.value = { frames: 1, height: args.h };
  mode.value = "capturing";
  try {
    await invoke("start_long_capture", args);
  } catch (e) {
    await reset();
    errMsg.value = "长截图启动失败: " + String(e);
  }
}

function cancelLong() {
  invoke("cancel_long_capture");
}

async function saveLong() {
  try {
    savedPath.value = await invoke("save_long_capture");
  } catch (e) {
    errMsg.value = String(e);
  }
}

/* ---------- 键盘 ---------- */
function onKeydown(e) {
  if (mode.value !== "shooting") return;
  if (e.key === "Escape") {
    e.preventDefault();
    cancel();
  } else if (e.key === "Enter") {
    e.preventDefault();
    confirmSelection();
  }
}

/* ---------- 事件 ---------- */
onMounted(async () => {
  listeners.push(
    await listen("screenshot-ready", (e) => onScreenshot(e.payload)),
  );
  listeners.push(
    await listen("long-capture-progress", (e) => (longInfo.value = e.payload)),
  );
  listeners.push(
    await listen("long-capture-done", (e) => {
      longResult.value = e.payload;
      mode.value = "preview";
    }),
  );
  listeners.push(
    await listen("long-capture-failed", async (e) => {
      await reset();
      errMsg.value = "长截图失败: " + e.payload.error;
    }),
  );
  window.addEventListener("keydown", onKeydown);
});

onBeforeUnmount(() => {
  listeners.forEach((u) => u && u());
  window.removeEventListener("keydown", onKeydown);
});
</script>

<template>
  <div class="root" :class="mode" @contextmenu.prevent>
    <!-- 待机：小窗口提示 -->
    <div v-if="mode === 'idle'" class="idle-panel">
      <h1>Qutter</h1>
      <p>按 <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>A</kbd> 开始截图</p>
      <button class="btn" @click="exitApp">退出</button>
      <p v-if="errMsg" class="err">{{ errMsg }}</p>
    </div>

    <!-- 框选：全屏截图 + 选区 + 操作栏 -->
    <template v-else-if="mode === 'shooting'">
      <canvas
        ref="canvasRef"
        class="shot-canvas"
        @mousedown="onMouseDown"
        @mousemove="onMouseMove"
        @mouseup="onMouseUp"
        @dblclick="confirmSelection"
      ></canvas>

      <div v-if="sel" class="sel-box" :style="selStyle">
        <span class="sel-tip" :style="{ top: sel.y < 32 ? '4px' : '-30px' }"
          >{{ sizeText }}</span
        >
        <span
          v-for="d in handles"
          :key="d"
          class="handle"
          :class="'dir-' + d"
          @mousedown.stop="startResize($event, d)"
          @dblclick.stop
        ></span>
      </div>

      <transition name="fade">
        <div v-if="!menuHidden" class="action-bar">
          <button class="btn btn-main" :disabled="!canConfirm" @click="confirmSelection">
            复制选区
          </button>
          <button class="btn btn-accent" :disabled="!canConfirm" @click="startLong">
            长截图
          </button>
          <button class="btn btn-ghost" @click="cancel">取消</button>
          <span class="bar-hint"><kbd>Enter</kbd> 复制 · <kbd>Esc</kbd> 取消</span>
        </div>
      </transition>
    </template>

    <!-- 截取中：角落小窗 -->
    <div v-else-if="mode === 'capturing'" class="capturing-panel">
      <div class="cap-row">
        <span class="cap-spinner"></span>
        <span class="cap-text"
          >长截图中… {{ longInfo.frames }} 帧 · {{ longInfo.height }}px</span
        >
      </div>
      <button class="btn btn-danger" @click="cancelLong">停止</button>
    </div>

    <!-- 预览确认 -->
    <div v-else-if="mode === 'preview'" class="preview-panel">
      <div class="preview-info">
        共 {{ longResult.frames }} 帧 · {{ longResult.width }} ×
        {{ longResult.height }}
        <span v-if="longResult.cancelled" class="warn"
          >（已停止，显示已截取部分）</span
        >
      </div>
      <div class="preview-scroll">
        <img :src="previewSrc" alt="长图预览" />
      </div>
      <div class="preview-actions">
        <button class="btn" @click="saveLong">保存</button>
        <button class="btn btn-ghost" @click="reset">放弃</button>
      </div>
      <p v-if="savedPath" class="saved">已保存到 {{ savedPath }}</p>
      <p v-if="errMsg" class="err">{{ errMsg }}</p>
    </div>
  </div>
</template>
