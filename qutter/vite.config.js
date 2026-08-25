import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";

// Tauri 2 官方推荐的 Vite 配置
export default defineConfig({
  plugins: [vue()],
  clearScreen: false,
  server: {
    port: 1420,
    strictPort: true,
    watch: {
      ignored: ["**/src-tauri/**"],
    },
  },
  build: {
    target: "es2021",
    outDir: "dist",
  },
});
