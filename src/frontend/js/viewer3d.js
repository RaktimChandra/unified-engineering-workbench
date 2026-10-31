// Shared three.js viewport: orbit (rotate), pan, zoom, fit-to-view, axes and
// a ground grid sized to the model. Rendering is on demand (no idle loop).
import * as THREE from "three";
import { OrbitControls } from "../vendor/three/OrbitControls.js";

export class Viewer3D {
  constructor(container) {
    this.container = container;
    this.renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.renderer.setClearColor(0xeef0f3, 1);
    container.appendChild(this.renderer.domElement);

    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(40, 1, 0.001, 1000);
    this.camera.up.set(0, 0, 1); // CAD convention: Z up
    this.camera.position.set(1, -1.4, 1);

    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = false;
    this.controls.screenSpacePanning = true;
    this.controls.addEventListener("change", () => this.render());

    this.scene.add(new THREE.HemisphereLight(0xffffff, 0x8a9099, 1.6));
    const key = new THREE.DirectionalLight(0xffffff, 1.6);
    key.position.set(1, -2, 3);
    this.scene.add(key);
    const fill = new THREE.DirectionalLight(0xffffff, 0.6);
    fill.position.set(-2, 1, -1);
    this.scene.add(fill);
    this.key = key;

    this.helpers = new THREE.Group();
    this.scene.add(this.helpers);
    this.content = new THREE.Group();
    this.scene.add(this.content);

    this._ro = new ResizeObserver(() => this.resize());
    this._ro.observe(container);
    this.resize();
  }

  resize() {
    const w = this.container.clientWidth || 1, h = this.container.clientHeight || 1;
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
    this.render();
  }

  render() {
    if (this._pending) return;
    this._pending = true;
    requestAnimationFrame(() => { this._pending = false; this.renderer.render(this.scene, this.camera); });
  }

  clear() {
    for (const c of [...this.content.children]) {
      this.content.remove(c);
      c.geometry?.dispose();
      if (c.material) (Array.isArray(c.material) ? c.material : [c.material]).forEach(m => m.dispose());
    }
    this.render();
  }

  add(obj) { this.content.add(obj); this.render(); }

  /** Rebuilds grid + axes around the current content bounding box. */
  _updateHelpers(box) {
    for (const c of [...this.helpers.children]) { this.helpers.remove(c); c.geometry?.dispose(); c.material?.dispose?.(); }
    const size = box.getSize(new THREE.Vector3());
    const span = Math.max(size.x, size.y, size.z) || 1;
    const step = Math.pow(10, Math.floor(Math.log10(span / 2)));
    const gridSize = Math.ceil((span * 2) / step) * step;
    const grid = new THREE.GridHelper(gridSize, Math.round(gridSize / step), 0xb5bbc4, 0xd8dce2);
    grid.rotation.x = Math.PI / 2; // into the XY plane (Z up)
    const c = box.getCenter(new THREE.Vector3());
    grid.position.set(c.x, c.y, box.min.z);
    this.helpers.add(grid);
    const axes = new THREE.AxesHelper(span * 0.35);
    axes.position.copy(box.min);
    this.helpers.add(axes);
    this.gridStep = step;
  }

  /** Frames all content with a margin, keeping the current view direction. */
  fit() {
    const box = new THREE.Box3().setFromObject(this.content);
    if (box.isEmpty()) return;
    this._updateHelpers(box);
    const center = box.getCenter(new THREE.Vector3());
    const radius = box.getBoundingSphere(new THREE.Sphere()).radius || 1;
    const dir = this.camera.position.clone().sub(this.controls.target);
    if (dir.lengthSq() === 0) dir.set(1, -1.4, 1);
    dir.normalize();
    const fov = THREE.MathUtils.degToRad(this.camera.fov);
    const dist = radius / Math.sin(Math.min(fov, fov * this.camera.aspect) / 2) * 1.08;
    this.camera.position.copy(center).addScaledVector(dir, dist);
    this.camera.near = Math.max(dist / 1000, 1e-6);
    this.camera.far = dist * 100;
    this.camera.updateProjectionMatrix();
    this.controls.target.copy(center);
    this.controls.update();
    this.key.position.copy(center).add(new THREE.Vector3(radius, -2 * radius, 3 * radius));
    this.render();
  }

  /** Resets to the default isometric-ish view and fits. */
  resetView() {
    this.camera.position.copy(this.controls.target).add(new THREE.Vector3(1, -1.4, 1));
    this.fit();
  }

  setView(name) {
    const v = { top: [0, 0, 1], front: [0, -1, 0.0001], right: [1, 0, 0.0001], iso: [1, -1.4, 1] }[name];
    if (!v) return;
    this.camera.position.copy(this.controls.target).add(new THREE.Vector3(...v));
    this.fit();
  }

  screenshot() { this.renderer.render(this.scene, this.camera); return this.renderer.domElement.toDataURL("image/png"); }

  dispose() { this._ro.disconnect(); this.renderer.dispose(); }
}

/** Builds a non-indexed triangle mesh from flat float32 arrays. */
export function buildMesh(positions, faceNormals, color = 0x8fa9c9) {
  const nt = positions.length / 9;
  const normals = new Float32Array(nt * 9);
  for (let t = 0; t < nt; t++) for (let k = 0; k < 3; k++) {
    normals[t * 9 + k * 3] = faceNormals[t * 3];
    normals[t * 9 + k * 3 + 1] = faceNormals[t * 3 + 1];
    normals[t * 9 + k * 3 + 2] = faceNormals[t * 3 + 2];
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.BufferAttribute(positions, 3));
  g.setAttribute("normal", new THREE.BufferAttribute(normals, 3));
  g.computeBoundingBox();
  const group = new THREE.Group();
  const mat = new THREE.MeshStandardMaterial({ color, metalness: 0.15, roughness: 0.55, side: THREE.DoubleSide,
    polygonOffset: true, polygonOffsetFactor: 1, polygonOffsetUnits: 1 });
  const mesh = new THREE.Mesh(g, mat);
  group.add(mesh);
  const edges = new THREE.LineSegments(new THREE.EdgesGeometry(g, 25),
    new THREE.LineBasicMaterial({ color: 0x2c3440, transparent: true, opacity: 0.55 }));
  group.add(edges);
  group.userData = { mesh, edges };
  return group;
}

export { THREE };
