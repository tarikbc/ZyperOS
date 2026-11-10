/* Reference-matched Pixel 2, built in Blender by tools/build-pixel2-model.py.
 * The GLB contains the product geometry and PBR materials. Only the display
 * texture and user-requested rotation change here; the product never follows
 * the mouse, floats or turns by itself.
 */
import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { RoomEnvironment } from "three/addons/environments/RoomEnvironment.js";

// The README's screenshots, all in the light theme, as on the page beside it
const SHOTS = ["shelf-n64", "systems", "favorites", "shelf-snes", "gallery", "shelf", "info", "cheevos", "menu", "mp4", "nowplaying", "gamemenu"];
const REST = { pitch: 0, yaw: -0.28, roll: 0 };

export async function mount(canvas) {
	const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
	renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
	renderer.toneMapping = THREE.ACESFilmicToneMapping;
	renderer.toneMappingExposure = 1.15;
	const scene = new THREE.Scene();
	const studio = new RoomEnvironment();
	const strip = new THREE.Mesh(new THREE.PlaneGeometry(2, 10), new THREE.MeshBasicMaterial({ color: 0xffffff, side: THREE.DoubleSide }));
	strip.material.color.multiplyScalar(4);
	strip.position.set(-4, 3, 5); strip.lookAt(0, 0, 0); studio.add(strip);
	const pmrem = new THREE.PMREMGenerator(renderer);
	const environment = pmrem.fromScene(studio, 0.04);
	scene.environment = environment.texture;
	pmrem.dispose(); studio.dispose();
	const key = new THREE.DirectionalLight(0xffffff, 2.1);
	key.position.set(-3, 4, 5); scene.add(key);
	const fill = new THREE.DirectionalLight(0xf1f6ff, 0.8);
	fill.position.set(4, 1, 2); scene.add(fill);
	const camera = new THREE.OrthographicCamera(-1, 1, 1, -1, 0.01, 20);
	camera.position.set(0, 0, 4);

	let model;
	try {
		model = (await new GLTFLoader().loadAsync(new URL("./models/pixel2.glb", import.meta.url).href)).scene;
	} catch (error) {
		environment.dispose(); renderer.dispose(); throw error;
	}
	// Centre the whole physical product, including buttons and shoulders.
	const bounds = new THREE.Box3().setFromObject(model);
	const centre = bounds.getCenter(new THREE.Vector3());
	const height = bounds.getSize(new THREE.Vector3()).y;
	model.scale.multiplyScalar(1 / height);
	model.position.copy(centre).multiplyScalar(-1 / height);
	model.traverse(obj => {
		if (obj.isMesh && obj.material) {
			const materials = Array.isArray(obj.material) ? obj.material : [obj.material];
			materials.forEach(m => { m.envMapIntensity = 1.1; });
		}
	});
	const device = new THREE.Group();
	device.rotation.order = "YXZ";
	device.add(model); scene.add(device);
	device.rotation.set(REST.pitch, REST.yaw, REST.roll, "YXZ");
	function draw() { renderer.render(scene, camera); }
	function fit() {
		const w = canvas.clientWidth, h = canvas.clientHeight;
		if (!w || !h) return;
		renderer.setSize(w, h, false);
		const aspect = w / h;
		// Fixed framing keeps the upright product centred during rotation.
		const halfHeight = Math.max(0.64, 0.55 / aspect);
		camera.left = -halfHeight * aspect; camera.right = halfHeight * aspect;
		camera.top = halfHeight; camera.bottom = -halfHeight;
		camera.updateProjectionMatrix(); draw();
	}

	// Named display mesh lets the physical model remain independent of OS art.
	const display = model.getObjectByName("Screen");
	const screen = document.createElement("canvas");
	screen.width = 1024; screen.height = 768;
	const context = screen.getContext("2d");
	const texture = new THREE.CanvasTexture(screen);
	texture.colorSpace = THREE.SRGBColorSpace;
	texture.flipY = false; // glTF UV convention
	texture.anisotropy = Math.min(8, renderer.capabilities.getMaxAnisotropy());
	if (display) {
		display.material.dispose();
		display.material = new THREE.MeshBasicMaterial({ map: texture, toneMapped: false });
	}
	async function loadImages() {
		const images = await Promise.all(SHOTS.map(async name => {
			const image = new Image();
			image.src = new URL(`./img/${name}.png`, import.meta.url).href;
			try { await image.decode(); return image; } catch { return null; }
		}));
		return images.filter(Boolean);
	}
	const available = await loadImages();
	let shot = 0;
	function showShot() {
		context.fillStyle = "#090c0c"; context.fillRect(0, 0, screen.width, screen.height);
		if (available.length) context.drawImage(available[shot % available.length], 0, 0, 1024, 768);
		texture.needsUpdate = true; draw();
	}

	let drag = null;
	canvas.addEventListener("pointerdown", event => {
		if (event.button !== 0) return;
		drag = { x: event.clientX, y: event.clientY, pitch: device.rotation.x, yaw: device.rotation.y, id: event.pointerId };
		canvas.setPointerCapture(event.pointerId);
	});
	canvas.addEventListener("pointermove", event => {
		if (!drag || event.pointerId !== drag.id) return;
		device.rotation.y = drag.yaw + (event.clientX - drag.x) * 0.009;
		if (event.pointerType === "mouse") device.rotation.x = Math.max(-0.65, Math.min(0.65, drag.pitch + (event.clientY - drag.y) * 0.006));
		draw();
	});
	const end = event => { if (drag?.id === event.pointerId) drag = null; };
	canvas.addEventListener("pointerup", end);
	canvas.addEventListener("pointercancel", end);
	canvas.addEventListener("lostpointercapture", end);
	canvas.addEventListener("keydown", event => {
		if (event.key === "ArrowLeft") device.rotation.y -= 0.12;
		else if (event.key === "ArrowRight") device.rotation.y += 0.12;
		else return;
		event.preventDefault(); draw();
	});
	const reduce = matchMedia("(prefers-reduced-motion: reduce)");
	let visible = false, timer = null;
	function schedule() {
		clearInterval(timer); timer = null;
		if (visible && !document.hidden && !reduce.matches && available.length > 1)
			timer = setInterval(() => { shot++; showShot(); }, 4000);
	}
	new IntersectionObserver(entries => { visible = entries[0].isIntersecting; schedule(); }).observe(canvas);
	document.addEventListener("visibilitychange", schedule);
	reduce.addEventListener("change", schedule);
	new ResizeObserver(fit).observe(canvas);
	fit(); showShot();
}
