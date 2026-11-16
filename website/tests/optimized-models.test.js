import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { DOMParser } from 'linkedom';

const root = resolve(import.meta.dirname, '..');
for (const model of [
  'environment/honeyhive/ForestHomePlanet',
  'environment/garden/DiskGardenPlanet',
  'models/StarPiece',
]) {
  test(`${model}: binary model preserves triangle count and valid texture references`, () => {
    const binary = readFileSync(resolve(root, 'public', model + '.glb'));
    const source = readFileSync(resolve(root, 'assets/model-sources', model + '.dae'), 'utf8');
    assert.equal(binary.readUInt32LE(0), 0x46546c67);
    assert.equal(binary.readUInt32LE(4), 2);
    assert.equal(binary.readUInt32LE(8), binary.length);
    const jsonLength = binary.readUInt32LE(12);
    const gltf = JSON.parse(binary.subarray(20, 20 + jsonLength).toString());
    const xml = new DOMParser().parseFromString(source, 'application/xml');
    const triangles = [...xml.querySelectorAll('polylist vcount')].reduce(
      (sum, node) =>
        sum +
        node.textContent
          .trim()
          .split(/\s+/)
          .reduce((count, vertices) => count + Number(vertices) - 2, 0),
      0,
    );
    const exportedTriangles = gltf.meshes.reduce(
      (sum, mesh) =>
        sum +
        mesh.primitives.reduce((count, primitive) => {
          assert.equal(primitive.mode ?? 4, 4);
          return (
            count + gltf.accessors[primitive.indices ?? primitive.attributes.POSITION].count / 3
          );
        }, 0),
      0,
    );
    assert.ok(triangles > 0);
    assert.equal(exportedTriangles, triangles);
    assert.ok(
      binary.length < source.length * 0.6,
      'Models should stay smaller than the XML source',
    );
    for (const image of gltf.images || []) {
      assert.ok(existsSync(resolve(root, 'public', dirname(model), image.uri)), image.uri);
    }
    const expectedTextures = model.startsWith('models/')
      ? []
      : [...xml.querySelectorAll('instance_material')].map((node) => {
          const material = xml.querySelector(node.getAttribute('target'));
          const effect = xml.querySelector(
            material.querySelector('instance_effect').getAttribute('url'),
          );
          const imageId = effect.querySelector('surface init_from').textContent;
          return xml
            .querySelector(`image[id="${imageId}"] init_from`)
            .textContent.replace(/\.png$/i, '.webp');
        });
    assert.deepEqual(
      new Set((gltf.images || []).map((image) => image.uri)),
      new Set(expectedTextures),
    );
  });
}
