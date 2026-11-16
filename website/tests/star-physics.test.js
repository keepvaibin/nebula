import test from 'node:test';
import assert from 'node:assert/strict';
import { Group, Mesh, PlaneGeometry, MeshBasicMaterial, Vector3 } from 'three';
import {
  buildTerrainCollider,
  updateTerrainCollider,
  keepOutside,
  stepBit,
} from '../star-physics.js';
function fixture() {
  const group = new Group();
  const floor = new Mesh(new PlaneGeometry(8, 8), new MeshBasicMaterial());
  floor.rotation.x = -Math.PI / 2;
  group.add(floor);
  const collider = {
    center: new Vector3(0, -1, 0),
    radius: 10,
    terrain: buildTerrainCollider(group),
  };
  updateTerrainCollider(collider);
  return { group, collider };
}
test('empty space inside the broad phase sphere does not cause a bounce', () => {
  const { collider } = fixture();
  const position = new Vector3(0, 2, 0),
    velocity = new Vector3();
  keepOutside(position, velocity, [collider], 0.2);
  assert.equal(position.y, 2);
  assert.equal(velocity.length(), 0);
});
test('terrain contact rebounds and gravity does not tunnel through the surface', () => {
  const { collider } = fixture();
  const position = new Vector3(0, 0.05, 0),
    velocity = new Vector3(0, -2, 0);
  keepOutside(position, velocity, [collider], 0.2);
  assert(position.y >= 0.2);
  assert(velocity.y > 0);
  const bit = {
    mesh: { position: new Vector3(0, 2, 0) },
    velocity: new Vector3(),
    scale: 0.2,
    speed: 0.2,
    phase: 0.7,
  };
  for (let frame = 0; frame < 2400; frame++) {
    stepBit(bit, collider, [collider], 1 / 120);
    assert(bit.mesh.position.y >= 0.21 - 1e-5);
  }
});
test('collision surfaces follow rotated and resized models', () => {
  const { group, collider } = fixture();
  group.rotation.z = 0.4;
  group.scale.setScalar(2);
  updateTerrainCollider(collider);
  const position = new Vector3(0, 0.05, 0).applyMatrix4(group.matrixWorld);
  keepOutside(position, null, [collider], 0.2);
  assert(position.clone().applyMatrix4(collider.terrain.inverse).y >= 0.1);
});
