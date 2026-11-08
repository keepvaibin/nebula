#include "Game/Scene/Scene.hpp"

Scene::Scene(const char* pName) : NerveExecutor(pName) {
    mListExecutor = nullptr;
    mSceneObjHolder = nullptr;
}

Scene::~Scene() {
    delete mSceneObjHolder;
    delete mListExecutor;
}

void Scene::init() {
}

void Scene::start() {
}

void Scene::update() {
}

void Scene::draw() const {
}

void Scene::calcAnim() {
}

void Scene::initNameObjListExecutor() {
    SceneNameObjListExecutor* exec = new SceneNameObjListExecutor();
    exec->init();
    mListExecutor = exec;
}

void Scene::initSceneObjHolder() {
    mSceneObjHolder = new SceneObjHolder();
}