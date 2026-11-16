import { pageMessage } from './page-copy.js';
import * as THREE from 'three';
import {
  keepOutside,
  stepBit,
  launchBit,
  buildTerrainCollider,
  updateTerrainCollider,
} from './star-physics.js';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';

const assetUrl = (path) => import.meta.env.BASE_URL + path.replace(/^\//, '');
const host = document.querySelector('#space');
const regenerateButton = document.querySelector('#regenerate');
const title = document.querySelector('#title');
const titleText = title.querySelector('.title-text');
let titlePop = null;
const status = document.querySelector('#collection-status');
const formationButton = document.querySelector('#formation');
const reduced = matchMedia('(prefers-reduced-motion: reduce)');
let paused = reduced.matches,
  formed = false,
  renderer,
  time = 0;
const bits = [];
const counter = document.querySelector('#star-count');
const sparkles = new Set();
function sparkle(x, y, trail = false) {
  if (paused || sparkles.size >= 12) return;
  const spark = document.createElement('span');
  spark.className = 'pickup-spark';
  spark.setAttribute('aria-hidden', 'true');
  spark.textContent = '✧';
  spark.style.left = x + 'px';
  spark.style.top = y + 'px';
  if (trail) {
    spark.style.fontSize = '11px';
    spark.style.opacity = '.65';
  }
  document.body.appendChild(spark);
  sparkles.add(spark);
  spark.addEventListener(
    'animationend',
    () => {
      spark.remove();
      sparkles.delete(spark);
    },
    { once: true },
  );
}
let collected = 0,
  previousPointer = null;
let gathering = null;
function syncControls() {
  title.classList.toggle('star-title', formed || !!gathering);
  formationButton.setAttribute('aria-pressed', String(formed));
  formationButton.textContent = pageMessage(formed ? 'releaseWord' : 'formWord');
}
syncControls();
const fontReady = document.fonts.load('145px Delfino');
try {
  renderer = new THREE.WebGLRenderer({
    alpha: true,
    antialias: true,
    powerPreference: 'low-power',
  });
  const touchDevice = matchMedia('(pointer: coarse)');
  renderer.setPixelRatio(Math.min(devicePixelRatio, touchDevice.matches ? 1.35 : 1.7));
  host.appendChild(renderer.domElement);
  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(40, 1, 0.1, 100);
  camera.position.z = 24;
  scene.add(new THREE.AmbientLight(0xffffff, 2));
  const key = new THREE.DirectionalLight(0xe6f2ff, 4);
  key.position.set(-4, 7, 10);
  scene.add(key);
  const rim = new THREE.DirectionalLight(0xa38bff, 3);
  rim.position.set(6, -2, 0);
  scene.add(rim);
  const starsGeometry = new THREE.BufferGeometry();
  const positions = [],
    colors = [];
  let seed = 184;
  const random = () => {
    seed = (seed * 16807) % 2147483647;
    return (seed - 1) / 2147483646;
  };
  for (let i = 0; i < 650; i++) {
    positions.push((random() - 0.5) * 65, (random() - 0.5) * 42, -random() * 25);
    const value = 0.25 + random() * 0.65;
    colors.push(value * 0.8, value * 0.9, value);
  }
  starsGeometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  starsGeometry.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
  scene.add(
    new THREE.Points(
      starsGeometry,
      new THREE.PointsMaterial({
        size: 0.026,
        vertexColors: true,
        transparent: true,
        opacity: 0.8,
      }),
    ),
  );
  let width, height;
  const orbitCenters = [new THREE.Vector3(), new THREE.Vector3()];
  const worlds = [];
  const colliders = [];
  const bitBatches = [];
  const hiddenMatrix = new THREE.Matrix4().makeScale(0, 0, 0);
  let previousFrame = performance.now();
  function positionWorlds() {
    if (!width || !height) return;
    const worldHeight = 2 * Math.tan(THREE.MathUtils.degToRad(20)) * 24;
    for (const world of worlds) {
      const rect = document.querySelector(world.selector).getBoundingClientRect();
      world.group.position.copy(orbitCenters[world.index]);
      world.group.scale.setScalar((rect.width / height) * worldHeight);
      world.collider.center.copy(world.group.position);
      world.collider.radius = world.boundRadius * world.group.scale.x;
      updateTerrainCollider(world.collider);
    }
    for (const b of bits)
      if (!formed && !b.collected)
        keepOutside(b.mesh.position, b.velocity, colliders, b.scale * 0.65 + 0.08);
  }
  function loadWorld(url, selector, index, rotation) {
    const manager = new THREE.LoadingManager();
    manager.onLoad = () => {
      positionWorlds();
      render();
    };
    new GLTFLoader(manager).load(
      assetUrl(url),
      (result) => {
        const model = result.scene;
        model.updateMatrixWorld(true);
        const box = new THREE.Box3().setFromObject(model),
          size = box.getSize(new THREE.Vector3()),
          center = box.getCenter(new THREE.Vector3());
        const normalizer = new THREE.Group();
        normalizer.add(model);
        // Preserve exported geometry, UVs, and per-vertex color; normalize only its placement.
        model.position.sub(center);
        normalizer.scale.setScalar(1 / Math.max(size.x, size.y, size.z));
        model.traverse((child) => {
          if (!child.isMesh) return;
          for (const material of [].concat(child.material)) {
            if (material.map) {
              material.map.colorSpace = THREE.SRGBColorSpace;
              material.map.anisotropy = Math.min(4, renderer.capabilities.getMaxAnisotropy());
            }
          }
        });
        const group = new THREE.Group();
        group.add(normalizer);
        group.rotation.set(...rotation);
        scene.add(group);
        const collider = {
          center: new THREE.Vector3(),
          radius: 0,
          index,
          terrain: buildTerrainCollider(group),
        };
        colliders.push(collider);
        worlds.push({
          group,
          selector,
          index,
          initialY: rotation[1],
          collider,
          boundRadius: size.length() / (2 * Math.max(size.x, size.y, size.z)),
        });
        positionWorlds();
        for (const b of bits)
          if (b.orbit === index && !formed && !b.collected) launchBit(b, collider);
        updateTargets();
        render();
      },
      undefined,
      (error) => console.warn('Environment asset could not load:', url, error),
    );
  }
  function updateOrbits() {
    const spaceRect = host.getBoundingClientRect();
    const worldHeight = 2 * Math.tan(THREE.MathUtils.degToRad(20)) * 24;
    document.querySelectorAll('.planet').forEach((planet, i) => {
      const r = planet.getBoundingClientRect();
      orbitCenters[i].set(
        ((r.left + r.width / 2 - spaceRect.left) / width - 0.5) * worldHeight * camera.aspect,
        (0.5 - (r.top + r.height / 2 - spaceRect.top) / height) * worldHeight,
        0,
      );
    });
  }
  function orbitPosition(b) {
    const center = orbitCenters[b.orbit];
    const angle = b.phase + time * b.speed * 0.7;
    const radius = b.radius;
    return new THREE.Vector3(
      center.x + Math.cos(angle) * radius,
      center.y + Math.sin(angle) * radius * 0.52,
      Math.sin(angle) * radius * 0.45 - 1,
    );
  }
  function resize() {
    const introHeight = Math.ceil(
      document.querySelector('.hero').getBoundingClientRect().bottom + window.scrollY,
    );
    host.style.height = introHeight + 'px';
    document.querySelector('.galaxy-scenery').style.height = introHeight + 'px';
    const nextWidth = host.clientWidth,
      nextHeight = host.clientHeight;
    // Mobile browsers fire resize as the address bar moves; reallocating an unchanged
    // drawing buffer would stall scrolling for no visible difference.
    if (nextWidth !== width || nextHeight !== height) {
      width = nextWidth;
      height = nextHeight;
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
      renderer.setSize(width, height);
    }
    updateOrbits();
    positionWorlds();
    updateTargets();
    if (formed && !gathering) placeFormation();
    render();
  }
  const palette = [0xa8e96e, 0xa985ef, 0x65bce9, 0xf5b566, 0xf2d771, 0xe58bba];
  // Sample the supplied angular wordmark rather than substituting a typeface.
  const wordImage = new Image();
  wordImage.src = assetUrl('/nebula-word-reference.webp');
  await wordImage.decode();
  const wordCanvas = document.createElement('canvas');
  wordCanvas.width = wordImage.naturalWidth;
  wordCanvas.height = wordImage.naturalHeight;
  const ctx = wordCanvas.getContext('2d');
  ctx.drawImage(wordImage, 0, 0);
  const pixels = ctx.getImageData(0, 0, wordCanvas.width, wordCanvas.height).data,
    targets = [];
  const inside = (x, y) => {
    if (x < 0 || y < 0 || x >= wordCanvas.width || y >= wordCanvas.height) return false;
    const i = (Math.floor(y) * wordCanvas.width + Math.floor(x)) * 4;
    return pixels[i + 3] > 128 && Math.max(pixels[i], pixels[i + 1], pixels[i + 2]) < 100;
  };
  const spacing = 6;
  for (let y = 5; y < wordCanvas.height; y += spacing)
    for (let x = 5; x < wordCanvas.width; x += spacing) {
      // Leave space for the complete spinning model at the silhouette's edge.
      const px = x + (random() - 0.5) * 1.8,
        py = y + (random() - 0.5) * 1.8;
      if (
        !inside(px, py) ||
        !Array.from({ length: 12 }, (_, i) => (i * Math.PI) / 6).every((a) =>
          inside(px + Math.cos(a) * 4, py + Math.sin(a) * 4),
        )
      )
        continue;
      targets.push({ x: px, y: py, position: new THREE.Vector3() });
    }
  const bounds = { left: 0, right: wordCanvas.width, top: 0, bottom: wordCanvas.height };
  let formationScale = 0.2;
  function frontDepth() {
    return Math.max(0, ...colliders.map((c) => c.center.z + c.radius + 0.8));
  }
  function updateTargets() {
    const rect = title.getBoundingClientRect(),
      spaceRect = host.getBoundingClientRect();
    const depth = frontDepth();
    const worldHeight = 2 * Math.tan(THREE.MathUtils.degToRad(20)) * (24 - depth);
    // Map the silhouette into the existing heading, at the camera's z=0 plane.
    const wordHeight = (rect.width * wordCanvas.height) / wordCanvas.width,
      top = rect.top - spaceRect.top + (rect.height - wordHeight) / 2;
    for (const target of targets) {
      const x =
        rect.left -
        spaceRect.left +
        ((target.x - bounds.left) / (bounds.right - bounds.left)) * rect.width;
      const y = top + ((target.y - bounds.top) / (bounds.bottom - bounds.top)) * wordHeight;
      target.position.set(
        (x / width - 0.5) * worldHeight * camera.aspect,
        (0.5 - y / height) * worldHeight,
        depth,
      );
    }
    formationScale = ((rect.width / (bounds.right - bounds.left)) * spacing * worldHeight) / height;
  }
  function placeFormation() {
    for (const b of bits) {
      b.mesh.position.copy(b.target.position);
      b.mesh.scale.setScalar(formationScale * b.wordSize);
    }
  }
  function gather(releasing = false) {
    titlePop?.cancel();
    if (paused) {
      gathering = null;
      if (formed) placeFormation();
      else
        for (const b of bits) {
          b.mesh.position.copy(b.returnTo || b.home);
          b.mesh.scale.setScalar(b.scale);
        }
      syncControls();
      return;
    }
    gathering = { start: performance.now(), duration: 1500, releasing };
    for (const [i, b] of bits.entries()) {
      if (!releasing) {
        b.returnTo = b.mesh.position.clone();
        b.returnVelocity = b.velocity.clone();
      }
      b.departure = b.mesh.position.clone();
      b.departureScale = b.mesh.scale.x;
      b.delay = (i % 11) * 18;
    }
    syncControls();
    titlePop = titleText.animate(
      releasing
        ? [
            { transform: 'scale(0)', opacity: 0, offset: 0 },
            { transform: 'scale(1.12)', opacity: 1, offset: 0.72 },
            { transform: 'scale(1)', opacity: 1, offset: 1 },
          ]
        : [
            { transform: 'scale(1)', opacity: 1, offset: 0 },
            { transform: 'scale(1.08)', opacity: 1, offset: 0.2 },
            { transform: 'scale(0)', opacity: 0, offset: 1 },
          ],
      {
        duration: releasing ? 650 : 500,
        delay: releasing ? 550 : 0,
        easing: 'cubic-bezier(.22,.61,.36,1)',
        fill: 'both',
      },
    );
    status.textContent = releasing ? pageMessage('releasing') : pageMessage('gathering');
  }
  new GLTFLoader().load(
    assetUrl('/models/StarPiece.glb'),
    (result) => {
      let geometry;
      result.scene.updateMatrixWorld(true);
      result.scene.traverse((child) => {
        if (child.isMesh && !geometry) {
          geometry = child.geometry.clone();
          geometry.applyMatrix4(child.matrixWorld);
        }
      });
      if (!geometry) return;
      geometry.center();
      geometry.computeBoundingBox();
      const size = geometry.boundingBox.getSize(new THREE.Vector3());
      geometry.scale(
        1 / Math.max(size.x, size.y, size.z),
        1 / Math.max(size.x, size.y, size.z),
        1 / Math.max(size.x, size.y, size.z),
      );
      geometry.computeVertexNormals();
      for (const [index, color] of palette.entries()) {
        const batch = new THREE.InstancedMesh(
          geometry,
          new THREE.MeshStandardMaterial({
            color,
            metalness: 0.28,
            roughness: 0.26,
            flatShading: true,
            emissive: color,
            emissiveIntensity: 0.13,
          }),
          Math.ceil((targets.length - index) / palette.length),
        );
        batch.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
        batch.frustumCulled = false;
        scene.add(batch);
        bitBatches.push(batch);
      }
      for (let i = 0; i < targets.length; i++) {
        const mesh = new THREE.Object3D();
        const prominent = i < 20;
        const x = (random() - 0.5) * 33,
          y = (random() - 0.5) * 19;
        const home = new THREE.Vector3(
          x,
          Math.abs(x) < 7 && Math.abs(y) < 5 ? (y < 0 ? -1 : 1) * (6 + random() * 3) : y,
          -2 + random() * 5,
        );
        mesh.position.copy(home);
        mesh.rotation.set(random() * 2, random() * 3, random() * 3);
        const scale = prominent ? 0.35 + random() * 0.55 : 0.08 + random() * 0.12;
        mesh.scale.setScalar(scale);
        bits.push({
          mesh,
          batch: bitBatches[i % palette.length],
          instance: Math.floor(i / palette.length),
          home,
          target: targets[i],
          wordSize: 0.65 + random() * 0.65,
          spin: new THREE.Vector3(
            (random() - 0.5) * 1.2,
            0.4 + random() * 1.4,
            (random() - 0.5) * 1.4,
          ),
          scale,
          phase: random() * Math.PI * 2,
          speed: 0.12 + random() * 0.18,
          collected: false,
          orbit: i % 2,
          radius: 1.8 + random() * 4.5,
          velocity: new THREE.Vector3(),
        });
      }
      updateOrbits();
      for (const b of bits) {
        b.home.copy(orbitPosition(b));
        b.mesh.position.copy(b.home);
        launchBit(
          b,
          colliders.find((c) => c.index === b.orbit),
        );
      }
      render();
    },
    undefined,
    () => {
      document.querySelector('#hint').textContent = pageMessage('fallbackHint');
      formationButton.disabled = true;
    },
  );
  function render() {
    for (const bit of bits) {
      bit.mesh.updateMatrix();
      bit.batch.setMatrixAt(bit.instance, bit.mesh.visible ? bit.mesh.matrix : hiddenMatrix);
    }
    for (const batch of bitBatches) batch.instanceMatrix.needsUpdate = true;
    renderer.render(scene, camera);
  }
  const bitMotionSpeed = 0.45;
  const bitSpinSpeed = 0.55;
  const destination = new THREE.Vector3();
  const scaleTarget = new THREE.Vector3();
  let sceneVisible = true;
  let lastDraw = 0;
  new IntersectionObserver(([entry]) => {
    sceneVisible = entry.isIntersecting;
    previousPointer = null;
  }).observe(host);
  function tick() {
    requestAnimationFrame(tick);
    const now = performance.now(),
      dt = Math.min((now - previousFrame) / 1000, 0.05);
    if (document.hidden || paused || !sceneVisible) {
      previousFrame = now;
      return;
    }
    if (touchDevice.matches && now - lastDraw < 1000 / 30) return;
    previousFrame = now;
    lastDraw = now;
    time += dt * 0.48;
    for (const world of worlds) {
      world.group.rotation.y = world.initialY + time * 0.025;
      updateTerrainCollider(world.collider);
    }
    const elapsed = gathering ? performance.now() - gathering.start : 0;
    for (const b of bits) {
      const dest = destination.copy(b.home);
      if (b.collected) {
        if (!b.mesh.visible) continue;
        const progress = THREE.MathUtils.clamp((now - b.pickupStart) / 260, 0, 1);
        const ease = progress * progress;
        b.mesh.position.lerpVectors(b.pickupFrom, b.pickup, ease);
        keepOutside(b.mesh.position, null, colliders, b.scale * 0.2);
        b.mesh.scale.setScalar(
          b.pickupScale * (1 + 0.22 * Math.sin(progress * Math.PI)) * (1 - Math.pow(progress, 3)),
        );
        b.mesh.rotation.y += dt * 15;
        if (progress < 0.9 && now - (b.lastTrail || 0) > 45) {
          const screen = b.mesh.position.clone().project(camera),
            rect = host.getBoundingClientRect();
          sparkle(
            (screen.x * 0.5 + 0.5) * width + rect.left,
            (-screen.y * 0.5 + 0.5) * height + rect.top,
            true,
          );
          b.lastTrail = now;
        }
        if (progress >= 1) {
          b.mesh.visible = false;
          sparkle(b.pickupScreen.x, b.pickupScreen.y);
        }
        continue;
      }
      if (gathering) {
        const progress = THREE.MathUtils.clamp((elapsed - b.delay) / 1200, 0, 1);
        const eased = progress * progress * (3 - 2 * progress);
        b.mesh.position.lerpVectors(
          b.departure,
          gathering.releasing ? b.returnTo || b.home : b.target.position,
          eased,
        );
        b.mesh.position.y += Math.sin(Math.PI * progress) * Math.sin(b.phase) * 1.2;
        b.mesh.scale.setScalar(
          THREE.MathUtils.lerp(
            b.departureScale,
            gathering.releasing ? b.scale : formationScale * b.wordSize,
            eased,
          ),
        );
        keepOutside(b.mesh.position, null, colliders, b.mesh.scale.x * 0.65 + 0.08);
        b.mesh.rotation.x += dt * bitSpinSpeed * b.spin.x;
        b.mesh.rotation.y += dt * bitSpinSpeed * b.spin.y;
        b.mesh.rotation.z += dt * bitSpinSpeed * b.spin.z;
        continue;
      }
      if (formed) {
        dest.copy(b.target.position);
      } else {
        const motionDt = dt * bitMotionSpeed;
        const steps = Math.max(1, Math.ceil(motionDt / (1 / 120)));
        for (let i = 0; i < steps; i++)
          stepBit(
            b,
            colliders.find((c) => c.index === b.orbit),
            colliders,
            motionDt / steps,
          );
      }
      if (formed) b.mesh.position.lerp(dest, 1 - Math.exp(-12 * dt));
      b.mesh.rotation.x += dt * bitSpinSpeed * b.spin.x;
      b.mesh.rotation.y += dt * bitSpinSpeed * b.spin.y;
      b.mesh.rotation.z += dt * bitSpinSpeed * b.spin.z;
      b.mesh.scale.lerp(
        scaleTarget.setScalar(formed ? formationScale * b.wordSize : b.scale),
        1 - Math.exp(-2.45 * dt),
      );
    }
    if (gathering && elapsed >= gathering.duration) {
      gathering = null;
      if (formed) placeFormation();
      else
        for (const b of bits) {
          if (b.returnVelocity) b.velocity.copy(b.returnVelocity);
        }
      syncControls();
      previousPointer = null;
      status.textContent = formed ? pageMessage('formed') : pageMessage('released');
    }
    render();
  }
  // Window resizes and hero size changes often arrive together; measure once per frame.
  let resizeQueued = false;
  const queueResize = () => {
    if (resizeQueued) return;
    resizeQueued = true;
    requestAnimationFrame(() => {
      resizeQueued = false;
      resize();
    });
  };
  window.addEventListener('resize', queueResize, { passive: true });
  new ResizeObserver(queueResize).observe(document.querySelector('.hero'));
  function collect(event) {
    if (!sceneVisible || gathering || event.target.closest('a,button,summary')) {
      previousPointer = null;
      return;
    }
    const rect = host.getBoundingClientRect(),
      point = new THREE.Vector2(event.clientX - rect.left, event.clientY - rect.top);
    if (point.y < 0 || point.y > height) {
      previousPointer = null;
      return;
    }
    const start = previousPointer || point,
      segment = point.clone().sub(start),
      length = segment.lengthSq();
    let changed = false;
    for (const b of bits) {
      if (b.collected) continue;
      const screen = b.mesh.position.clone().project(camera),
        center = new THREE.Vector2(
          (screen.x * 0.5 + 0.5) * width,
          (-screen.y * 0.5 + 0.5) * height,
        );
      const along = length
        ? THREE.MathUtils.clamp(center.clone().sub(start).dot(segment) / length, 0, 1)
        : 0;
      const nearest = start.clone().addScaledVector(segment, along);
      const radius = Math.max(
        12,
        ((b.mesh.scale.x * height) /
          (2 * Math.tan(THREE.MathUtils.degToRad(20)) * (24 - b.mesh.position.z))) *
          0.6,
      );
      if (center.distanceTo(nearest) < radius) {
        b.collected = true;
        collected++;
        counter.textContent = collected;
        changed = true;
        const depth = frontDepth(),
          worldHeight = 2 * Math.tan(THREE.MathUtils.degToRad(20)) * (24 - depth);
        b.pickup = new THREE.Vector3(
          (point.x / width - 0.5) * worldHeight * camera.aspect,
          (0.5 - point.y / height) * worldHeight,
          depth,
        );
        b.pickupFrom = b.mesh.position.clone();
        b.pickupStart = performance.now();
        b.pickupScale = b.mesh.scale.x;
        b.pickupScreen = { x: event.clientX, y: event.clientY };
        if (paused) b.mesh.visible = false;
      }
    }
    previousPointer = point;
    if (changed) {
      status.textContent = pageMessage('collected', { count: collected });
      render();
    }
  }
  window.addEventListener('pointermove', collect, { passive: true });
  window.addEventListener('pointerdown', collect, { passive: true });
  window.addEventListener('pointerout', (event) => {
    if (!event.relatedTarget) previousPointer = null;
  });
  window.addEventListener(
    'scroll',
    () => {
      previousPointer = null;
    },
    { passive: true },
  );
  function restore(preserveScale = false) {
    collected = 0;
    counter.textContent = '0';
    previousPointer = null;
    for (const b of bits) {
      if (b.collected) b.mesh.scale.setScalar(b.scale);
      b.collected = false;
      b.mesh.visible = true;
      if (!preserveScale) b.mesh.scale.setScalar(formed ? formationScale * b.wordSize : b.scale);
      if (!formed && !preserveScale)
        launchBit(
          b,
          colliders.find((c) => c.index === b.orbit),
        );
    }
  }
  regenerateButton.addEventListener('click', () => {
    collected = 0;
    counter.textContent = '0';
    previousPointer = null;
    for (const b of bits) {
      // Replenish missing bits without disturbing the ones already in motion.
      if (!b.collected) continue;
      b.collected = false;
      b.mesh.visible = true;
      if (formed) b.mesh.position.copy(b.target.position);
      else {
        b.mesh.position.copy(b.home);
        launchBit(
          b,
          colliders.find((c) => c.index === b.orbit),
        );
      }
      b.mesh.scale.setScalar(paused ? (formed ? formationScale * b.wordSize : b.scale) : 0);
    }
    if (!gathering) status.textContent = pageMessage('regenerated');
    render();
  });
  formationButton.addEventListener('click', () => {
    if (!bits.length) return;
    gathering = null;
    formed = !formed;
    restore(true);
    syncControls();
    updateTargets();
    gather(!formed);
    render();
  });
  fontReady.then(resize);
  reduced.addEventListener('change', (event) => {
    paused = event.matches;
    if (paused && gathering) {
      gather(!formed);
    }
    syncControls();
    render();
  });
  resize();
  loadWorld('/environment/honeyhive/ForestHomePlanet.glb', '.planet.moon', 0, [0.4, -0.65, 0.12]);
  loadWorld('/environment/garden/DiskGardenPlanet.glb', '.planet.garden', 1, [0.4, -0.55, -0.25]);
  tick();
} catch (error) {
  host.style.backgroundImage =
    'radial-gradient(circle at 15% 25%, #7d61a733, transparent 30%), radial-gradient(circle at 80% 70%, #6a925622, transparent 30%)';
  regenerateButton.hidden = true;
  formationButton.hidden = true;
  document.querySelector('#hint').hidden = true;
  console.info('Static space background enabled.', error);
}
