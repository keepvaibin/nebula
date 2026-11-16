import { Vector3, Matrix4, BufferGeometry, Float32BufferAttribute } from 'three';
import { MeshBVH } from 'three-mesh-bvh';

// Reuse working vectors in the synchronous physics loop instead of allocating per bit/substep.
const localPoint = new Vector3();
const contactNormal = new Vector3();
const nearestHit = { point: new Vector3(), distance: 0, faceIndex: 0 };
const inwardVector = new Vector3();
const outwardVector = new Vector3();
const tangentVector = new Vector3();
const worldScale = new Vector3();

// Build once in the planet group's local coordinates; its live transform follows rotation/resize.
export function buildTerrainCollider(group) {
  group.updateMatrixWorld(true);
  const inverse = new Matrix4().copy(group.matrixWorld).invert();
  const chunks = [];
  let length = 0;
  group.traverse((child) => {
    if (!child.isMesh) return;
    const geometry = child.geometry.index ? child.geometry.toNonIndexed() : child.geometry.clone();
    geometry.applyMatrix4(new Matrix4().multiplyMatrices(inverse, child.matrixWorld));
    const positions = geometry.getAttribute('position').array;
    chunks.push(positions);
    length += positions.length;
    geometry.dispose();
  });
  const positions = new Float32Array(length);
  let offset = 0;
  for (const chunk of chunks) {
    positions.set(chunk, offset);
    offset += chunk.length;
  }
  const geometry = new BufferGeometry();
  geometry.setAttribute('position', new Float32BufferAttribute(positions, 3));
  return { group, bvh: new MeshBVH(geometry), inverse: new Matrix4(), scale: 1 };
}

export function updateTerrainCollider(collider) {
  if (!collider.terrain) return;
  collider.terrain.group.updateMatrixWorld(true);
  collider.terrain.inverse.copy(collider.terrain.group.matrixWorld).invert();
  collider.terrain.scale = collider.terrain.group.getWorldScale(worldScale).x;
}

export function keepOutside(position, velocity, colliders, padding = 0) {
  let contacts = 0;
  for (let pass = 0; pass < 3; pass++)
    for (const collider of colliders) {
      // The old sphere is only a cheap query filter; it never pushes or bounces a bit.
      if (!collider.terrain || position.distanceTo(collider.center) > collider.radius + padding)
        continue;
      const { bvh, inverse, scale, group } = collider.terrain;
      const local = localPoint.copy(position).applyMatrix4(inverse),
        radius = padding / scale;
      const nearest = bvh.closestPointToPoint(local, nearestHit, 0, radius);
      if (!nearest || nearest.distance >= radius) continue;
      contacts++;
      const normal = contactNormal.copy(local).sub(nearest.point);
      if (normal.lengthSq() < 1e-12) {
        if (velocity && velocity.lengthSq() > 1e-12)
          normal.copy(velocity).transformDirection(inverse).negate();
        else normal.set(0, 1, 0);
      } else normal.normalize();
      local.copy(nearest.point).addScaledVector(normal, radius + 0.001 / scale);
      position.copy(local).applyMatrix4(group.matrixWorld);
      normal.transformDirection(group.matrixWorld);
      if (velocity) {
        const incoming = velocity.dot(normal);
        if (incoming < 0) velocity.addScaledVector(normal, -1.82 * incoming);
        const outgoing = velocity.dot(normal);
        if (outgoing < 0.8) velocity.addScaledVector(normal, 0.8 - outgoing);
      }
    }
  return contacts;
}

export function stepBit(bit, collider, colliders, dt) {
  if (!collider) return;
  const inward = inwardVector.copy(collider.center).sub(bit.mesh.position);
  const distance = Math.max(inward.length(), 0.01);
  inward.divideScalar(distance);
  const gravity = 1.05 + bit.speed * 0.35 + Math.max(0, distance - collider.radius - 3.6) * 0.9;
  bit.velocity.addScaledVector(inward, gravity * dt);
  bit.mesh.position.addScaledVector(bit.velocity, dt);
  const contacts = keepOutside(bit.mesh.position, bit.velocity, colliders, bit.scale * 0.65 + 0.08);
  if (contacts) {
    // Replenish broad, varied flight arcs rather than letting bits collect in concave scenery.
    const outward = outwardVector.copy(bit.mesh.position).sub(collider.center).normalize();
    const radial = bit.velocity.dot(outward),
      kick = 0.65 + bit.speed * 1.1;
    if (radial < kick) bit.velocity.addScaledVector(outward, kick - radial);
    const tangent = tangentVector.set(-outward.y, outward.x, Math.sin(bit.phase) * 0.6).normalize();
    const sideways = bit.velocity.dot(tangent),
      cruise = 0.8 + bit.speed * 1.4;
    if (sideways < cruise) bit.velocity.addScaledVector(tangent, (cruise - sideways) * 0.3);
    keepOutside(bit.mesh.position, null, colliders, bit.scale * 0.65 + 0.08);
  }
}

export function launchBit(bit, collider) {
  if (!collider) return;
  const normal = new Vector3(
    Math.cos(bit.phase),
    Math.sin(bit.phase),
    0.5 * Math.sin(bit.phase * 2),
  ).normalize();
  const radius = collider.radius + bit.scale * 0.65 + 0.35 + (bit.radius % 2.1);
  bit.mesh.position.copy(collider.center).addScaledVector(normal, radius);
  const tangent = new Vector3(-normal.y, normal.x, 0.3 * Math.cos(bit.phase)).normalize();
  bit.velocity
    .copy(tangent)
    .multiplyScalar(1.0 + bit.speed * 2)
    .addScaledVector(normal, 0.25 + bit.speed * 0.5);
  bit.home.copy(bit.mesh.position);
}
