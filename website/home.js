// Let the useful page content paint before downloading and initializing the 3D scene.
const startScene = () => import('./main.js');
if ('requestIdleCallback' in window) requestIdleCallback(startScene, { timeout: 300 });
else requestAnimationFrame(() => setTimeout(startScene, 0));
