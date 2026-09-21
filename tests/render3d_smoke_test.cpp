// Smoke test : render3d:: (lib/render3d/) — Mesh::Cube/Sphere/Plane (CPU
// pur, sans device) puis Canvas bout en bout (device réel) avec les shaders
// BasicShader()/PhongShader() intégrés.
#define USE_TEST
#include "core/test.hpp"
#include "render3d/animation.hpp"
#include "render3d/canvas.hpp"
#include "render3d/instanced_mesh.hpp"
#include "render3d/lod.hpp"
#include "render3d/point_line.hpp"
#include "render3d/shape.hpp"
#include "render3d/skinned_mesh.hpp"
#include "render3d/sprite.hpp"
#include "sdl3/sdl3.hpp"
#include <array>
#include <cmath>
#include <map>

using namespace render3d;

namespace {

/// Pour chaque triangle, vérifie que (b-a)×(c-a) (normale géométrique de la
/// face, règle de la main droite) pointe globalement dans la même direction
/// que la moyenne des normales de sommet — l'invariant exact requis par le
/// pipeline de Canvas (front_face=CLOCKWISE compense le flip Y de
/// FMatrix4::Perspective() pour ce winding précis, voir canvas.hpp) : le
/// détecter ici, sur CPU, évite d'avoir à rendre pour repérer un sens
/// d'enroulement inversé.
void ExpectConsistentWinding(const Mesh &mesh) {
    const auto &verts = mesh.Vertices();
    const auto &idx = mesh.Indices();
    int checked = 0;
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        math::FVector3 a = verts[idx[i]].position, b = verts[idx[i + 1]].position, c = verts[idx[i + 2]].position;
        math::FVector3 faceNormal = (b - a).Cross(c - a);
        if (faceNormal.LengthSq() < 1e-10f)
            continue; // triangle dégénéré (ex. pôle d'une sphère) : ignoré
        math::FVector3 avgNormal = verts[idx[i]].normal + verts[idx[i + 1]].normal + verts[idx[i + 2]].normal;
        ++checked;
        EXPECT_TRUE(faceNormal.Dot(avgNormal) > 0.f);
    }
    EXPECT_TRUE(checked > 0);
}

/// Vérifie qu'un maillage est topologiquement fermé (2-variété) : chaque
/// arête non orientée est partagée par exactement 2 triangles — même sans
/// rendu, une erreur de transcription dans les données d'un polyèdre codé en
/// dur (Icosahedron/Dodecahedron) casse quasi-systématiquement cet invariant.
/// Clé par POSITION arrondie plutôt que par indice : Polyhedron() produit une
/// sortie non indexée (trois sommets propres par triangle, même à une arête
/// partagée), donc deux triangles adjacents n'ont jamais le même indice de
/// sommet même si leurs positions coïncident.
void ExpectWatertightManifold(const Mesh &mesh) {
    auto roundKey = [](const math::FVector3 &p) -> std::array<int, 3> {
        constexpr float SCALE = 10000.f;
        return {int(std::round(p.x * SCALE)), int(std::round(p.y * SCALE)), int(std::round(p.z * SCALE))};
    };

    std::map<std::pair<std::array<int, 3>, std::array<int, 3>>, int> edgeCount;
    const auto &verts = mesh.Vertices();
    const auto &idx = mesh.Indices();
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        std::array<int, 3> keys[3] = {roundKey(verts[idx[i]].position), roundKey(verts[idx[i + 1]].position),
                                      roundKey(verts[idx[i + 2]].position)};
        for (int e = 0; e < 3; ++e) {
            auto k0 = keys[e], k1 = keys[(e + 1) % 3];
            auto key = k0 < k1 ? std::make_pair(k0, k1) : std::make_pair(k1, k0);
            ++edgeCount[key];
        }
    }
    bool allPaired = true;
    for (const auto &[edge, count] : edgeCount)
        if (count != 2)
            allPaired = false;
    EXPECT_TRUE(allPaired);
}

} // namespace

// ── Mesh — génération procédurale, aucun device requis ──────────────────────

TEST(Render3dMesh, CubeInvariants) {
    Mesh cube = Mesh::Cube(2.f);
    EXPECT_EQ(cube.Vertices().size(), size_t(24));
    EXPECT_EQ(cube.IndexCount(), uint32_t(36));
    EXPECT_EQ(cube.Groups().size(), size_t(6)); // un groupe par face (Shape par-face material)

    for (const auto &v : cube.Vertices()) {
        float len = v.normal.Length();
        EXPECT_TRUE(std::abs(len - 1.f) < 1e-5f);
        EXPECT_TRUE(std::abs(v.position.x) <= 1.f + 1e-5f);
        EXPECT_TRUE(std::abs(v.position.y) <= 1.f + 1e-5f);
        EXPECT_TRUE(std::abs(v.position.z) <= 1.f + 1e-5f);
    }
    ExpectConsistentWinding(cube);
}

TEST(Render3dMesh, SphereInvariants) {
    Mesh sphere = Mesh::Sphere(3.f, 12, 8);
    EXPECT_TRUE(!sphere.Vertices().empty());
    EXPECT_TRUE(sphere.IndexCount() > 0);
    EXPECT_TRUE(sphere.IndexCount() % 3 == 0);

    for (const auto &v : sphere.Vertices()) {
        float radius = v.position.Length();
        EXPECT_TRUE(std::abs(radius - 3.f) < 1e-3f);
        EXPECT_TRUE(std::abs(v.normal.Length() - 1.f) < 1e-4f);
    }
    ExpectConsistentWinding(sphere);
}

TEST(Render3dMesh, PlaneInvariants) {
    Mesh plane = Mesh::Plane(4.f, 2.f, 2, 2);
    EXPECT_EQ(plane.Vertices().size(), size_t(9)); // (2+1) x (2+1)
    EXPECT_EQ(plane.IndexCount(), uint32_t(24));   // 2*2 cellules x 2 triangles x 3

    for (const auto &v : plane.Vertices()) {
        EXPECT_TRUE(v.position.y == 0.f);
        EXPECT_TRUE(v.normal.y == 1.f);
        EXPECT_TRUE(std::abs(v.position.x) <= 2.f + 1e-5f);
        EXPECT_TRUE(std::abs(v.position.z) <= 1.f + 1e-5f);
    }
    ExpectConsistentWinding(plane);
}

TEST(Render3dMesh, CylinderInvariants) {
    Mesh cyl = Mesh::Cylinder(1.f, 1.f, 2.f, 16, 1, false);
    EXPECT_TRUE(!cyl.Vertices().empty());
    for (const auto &v : cyl.Vertices()) {
        EXPECT_TRUE(std::abs(v.position.y) <= 1.f + 1e-4f);
        if (std::abs(std::abs(v.position.y) - 1.f) > 1e-4f) // pas sur un capuchon : rayon == 1
            EXPECT_TRUE(std::abs(std::sqrt(v.position.x * v.position.x + v.position.z * v.position.z) - 1.f) < 1e-3f);
    }
    ExpectConsistentWinding(cyl);
}

TEST(Render3dMesh, ConeInvariants) {
    Mesh cone = Mesh::Cone(1.f, 2.f, 16, 1, false);
    EXPECT_TRUE(!cone.Vertices().empty());
    // La pointe (y = +1, sommet du cône) doit avoir un rayon quasi nul.
    bool foundApexNearby = false;
    for (const auto &v : cone.Vertices())
        if (v.position.y > 0.99f)
            foundApexNearby = true;
    EXPECT_TRUE(foundApexNearby);
    ExpectConsistentWinding(cone);
}

TEST(Render3dMesh, TorusInvariants) {
    Mesh torus = Mesh::Torus(2.f, 0.5f, 8, 16);
    EXPECT_TRUE(!torus.Vertices().empty());
    for (const auto &v : torus.Vertices()) {
        // Distance du sommet à l'axe Z (le cercle central du tore, rayon 2)
        // doit être dans [2-tube, 2+tube].
        float distToCenterCircleAxis = std::sqrt(v.position.x * v.position.x + v.position.y * v.position.y);
        EXPECT_TRUE(distToCenterCircleAxis >= 1.5f - 1e-3f && distToCenterCircleAxis <= 2.5f + 1e-3f);
        EXPECT_TRUE(std::abs(v.normal.Length() - 1.f) < 1e-3f);
    }
    ExpectConsistentWinding(torus);
}

TEST(Render3dMesh, CircleInvariants) {
    Mesh circle = Mesh::Circle(2.f, 16);
    for (const auto &v : circle.Vertices()) {
        EXPECT_TRUE(v.position.y == 0.f);
        EXPECT_TRUE(v.normal.y == 1.f);
        EXPECT_TRUE(v.position.Length() <= 2.f + 1e-3f);
    }
    ExpectConsistentWinding(circle);
}

TEST(Render3dMesh, RingInvariants) {
    Mesh ring = Mesh::Ring(0.5f, 1.f, 16, 1);
    for (const auto &v : ring.Vertices()) {
        float r = v.position.Length();
        EXPECT_TRUE(r >= 0.5f - 1e-3f && r <= 1.f + 1e-3f);
    }
    ExpectConsistentWinding(ring);
}

TEST(Render3dMesh, PolyhedraInvariants) {
    Mesh tetra = Mesh::Tetrahedron(1.5f, 0);
    Mesh octa = Mesh::Octahedron(1.5f, 1);
    Mesh icosa = Mesh::Icosahedron(1.5f, 0);
    Mesh dodeca = Mesh::Dodecahedron(1.5f, 0);

    for (Mesh *mesh : {&tetra, &octa, &icosa, &dodeca}) {
        EXPECT_TRUE(!mesh->Vertices().empty());
        EXPECT_TRUE(mesh->IndexCount() % 3 == 0);
        for (const auto &v : mesh->Vertices()) {
            EXPECT_TRUE(std::abs(v.position.Length() - 1.5f) < 1e-3f);
            EXPECT_TRUE(std::abs(v.normal.Length() - 1.f) < 1e-3f);
        }
        ExpectConsistentWinding(*mesh);
        ExpectWatertightManifold(*mesh);
    }

    // Un niveau de détail doit multiplier le nombre de triangles par 4.
    Mesh icosaDetail1 = Mesh::Icosahedron(1.f, 1);
    EXPECT_EQ(icosaDetail1.IndexCount(), icosa.IndexCount() * 4);
}

TEST(Render3dMesh, TubeInvariants) {
    LineCurve3 line({0.f, 0.f, 0.f}, {0.f, 0.f, 10.f});
    Mesh tube = Mesh::Tube(line, 8, 0.5f, 8, false);
    EXPECT_TRUE(!tube.Vertices().empty());
    for (const auto &v : tube.Vertices()) {
        EXPECT_TRUE(v.position.z >= -1e-3f && v.position.z <= 10.f + 1e-3f);
        float radialDist = std::sqrt(v.position.x * v.position.x + v.position.y * v.position.y);
        EXPECT_TRUE(std::abs(radialDist - 0.5f) < 1e-3f);
    }
    ExpectConsistentWinding(tube);
}

TEST(Render3dMesh, TorusKnotInvariants) {
    Mesh knot = Mesh::TorusKnot(2.f, 0.3f, 64, 8, 2, 3);
    EXPECT_TRUE(!knot.Vertices().empty());
    EXPECT_TRUE(knot.IndexCount() % 3 == 0);
    for (const auto &v : knot.Vertices())
        EXPECT_TRUE(std::abs(v.normal.Length() - 1.f) < 1e-2f);
    ExpectConsistentWinding(knot);
}

TEST(Render3dMesh, LatheInvariants) {
    // Profil : un cône simple (rayon 1 à y=0, rayon 0 à y=2).
    math::FVector2 profile[2] = {{1.f, 0.f}, {0.f, 2.f}};
    Mesh lathe = Mesh::Lathe(profile, 12);
    EXPECT_TRUE(!lathe.Vertices().empty());
    for (const auto &v : lathe.Vertices())
        EXPECT_TRUE(v.position.y >= -1e-3f && v.position.y <= 2.f + 1e-3f);
    ExpectConsistentWinding(lathe);
}

TEST(Render3dMesh, ExtrudeInvariants) {
    Shape2D square({{0.f, 0.f}, {2.f, 0.f}, {2.f, 2.f}, {0.f, 2.f}});
    Mesh extruded = Mesh::Extrude(square, 1.f);
    EXPECT_TRUE(!extruded.Vertices().empty());
    for (const auto &v : extruded.Vertices())
        EXPECT_TRUE(std::abs(v.position.z) <= 0.5f + 1e-3f);
    ExpectConsistentWinding(extruded);

    Shape2D squareWithHole({{0.f, 0.f}, {4.f, 0.f}, {4.f, 4.f}, {0.f, 4.f}});
    squareWithHole.AddHole({{1.f, 1.f}, {1.f, 2.f}, {2.f, 2.f}, {2.f, 1.f}});
    Mesh extrudedWithHole = Mesh::Extrude(squareWithHole, 1.f);
    EXPECT_TRUE(!extrudedWithHole.Vertices().empty());
    ExpectConsistentWinding(extrudedWithHole);
}

// ── Canvas — bout en bout, device GPU réel ──────────────────────────────────

TEST(Render3dCanvas, DrawProceduralMeshesEndToEnd) {
    auto ctx = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
    ASSERT_TRUE(ctx.IsOk());

    auto windowResult = sdl3::Window::Create(String("render3d_smoke_test"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    sdl3::Window window = std::move(windowResult.Value());

    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());

    Camera camera;
    camera.position = {0.f, 0.f, -5.f};
    camera.aspect = 1.f;
    canvas.SetCamera(camera);
    canvas.SetLighting(DirectionalLight{}, AmbientLight{});
    canvas.SetBackgroundColor(sdl3::Color::DARK_GREY1());

    Mesh cube = Mesh::Cube();
    Mesh sphere = Mesh::Sphere();
    Mesh plane = Mesh::Plane();

    Material lit = Material::Default();
    Material unlit = Material::Unlit(sdl3::Color::RED());

    bool began = canvas.Begin();
    if (began) {
        canvas.DrawMesh(MakeRefMut(cube), math::FMatrix4::Translate(-2.f, 0.f, 0.f), lit);
        canvas.DrawMesh(MakeRefMut(sphere), math::FMatrix4::Translate(0.f, 0.f, 0.f), lit);
        canvas.DrawMesh(MakeRefMut(plane), math::FMatrix4::Translate(2.f, 0.f, 0.f), unlit);
        canvas.End();
    }
    // began peut être faux si la fenêtre n'est jamais présentée par le
    // compositeur (cas normal en environnement sandboxé) — dans ce cas
    // aucune frame n'a été soumise, mais la création du pipeline complet et
    // l'upload des maillages doivent tout de même s'être déroulés sans erreur.

    EXPECT_TRUE(sdl3::GetError().IsEmpty());
    EXPECT_TRUE(cube.HasGpuBuffers() || !began);
}

TEST(Render3dCanvas, MultiLightSceneEndToEnd) {
    auto ctx = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
    ASSERT_TRUE(ctx.IsOk());

    auto windowResult = sdl3::Window::Create(String("render3d_smoke_test_lights"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    sdl3::Window window = std::move(windowResult.Value());

    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());

    Camera camera;
    camera.position = {0.f, 0.f, -5.f};
    camera.aspect = 1.f;
    canvas.SetCamera(camera);
    canvas.SetLighting(DirectionalLight{}, AmbientLight{});

    PointLight redPoint;
    redPoint.position = {-2.f, 0.f, 0.f};
    redPoint.color = sdl3::Color::RED();
    PointLight bluePoint;
    bluePoint.position = {2.f, 0.f, 0.f};
    bluePoint.color = sdl3::Color::BLUE();

    SpotLight spot;
    spot.position = {0.f, 2.f, -2.f};
    spot.direction = {0.f, -1.f, 0.5f};
    spot.color = sdl3::Color::WHITE();
    spot.angle = 0.5f;
    spot.penumbra = 0.3f;

    PointLight points[2] = {redPoint, bluePoint};
    SpotLight spots[1] = {spot};
    canvas.SetLights(points, spots);

    Mesh cube = Mesh::Cube();
    Material lit = Material::Default();

    bool began = canvas.Begin();
    if (began) {
        canvas.DrawMesh(MakeRefMut(cube), math::FMatrix4::Identity(), lit);
        canvas.End();
    }

    EXPECT_TRUE(sdl3::GetError().IsEmpty());
    EXPECT_TRUE(cube.HasGpuBuffers() || !began);

    // Un deuxième Begin()/End() avec le même compte de lumières doit réutiliser
    // le pipeline mis en cache (PipelineKey), pas en recompiler un nouveau.
    began = canvas.Begin();
    if (began) {
        canvas.DrawMesh(MakeRefMut(cube), math::FMatrix4::Identity(), lit);
        canvas.End();
    }
    EXPECT_TRUE(sdl3::GetError().IsEmpty());
}

// ── Object3D — composition de hiérarchie, CPU pur ───────────────────────────

TEST(Render3dObject3D, WorldMatrixComposition) {
    auto root = std::make_unique<Object3D>();
    root->SetRotation(math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, 3.14159265f * 0.5f));

    auto childOwned = std::make_unique<Object3D>();
    childOwned->SetPosition({1.f, 0.f, 0.f});
    Object3D &child = root->Add(std::move(childOwned));

    EXPECT_TRUE(child.Parent() == root.get());
    EXPECT_EQ(root->Children().size(), size_t(1));

    // La position monde de l'origine locale de l'enfant doit correspondre à
    // l'enfant tourné par le quaternion du parent (même Rotate() déjà vérifié
    // indépendamment par MathQuaternion::SlerpAndRotate) — vérifie que la
    // composition Object3D (ComposeTRS + chaîne de multiplication de
    // matrices) produit le même résultat que la rotation directe du vecteur.
    math::FVector3 childWorldPos = child.WorldMatrix().TransformPoint({0.f, 0.f, 0.f});
    math::FVector3 expected = root->Rotation().Rotate({1.f, 0.f, 0.f});
    EXPECT_TRUE(std::abs(childWorldPos.x - expected.x) < 1e-4f);
    EXPECT_TRUE(std::abs(childWorldPos.y - expected.y) < 1e-4f);
    EXPECT_TRUE(std::abs(childWorldPos.z - expected.z) < 1e-4f);

    int visited = 0;
    root->Traverse([&](Object3D &) { ++visited; });
    EXPECT_EQ(visited, 2); // root + child

    root->SetVisible(false);
    visited = 0;
    root->Traverse([&](Object3D &) { ++visited; });
    EXPECT_EQ(visited, 0); // sous-arbre invisible entièrement sauté
}

// ── Shape + DrawObject — bout en bout, device GPU réel ──────────────────────

TEST(Render3dCanvas, ShapeHierarchyDrawObject) {
    auto ctx = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
    ASSERT_TRUE(ctx.IsOk());

    auto windowResult = sdl3::Window::Create(String("render3d_smoke_test_shape"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    sdl3::Window window = std::move(windowResult.Value());

    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());

    Camera camera;
    camera.position = {0.f, 0.f, -8.f};
    camera.aspect = 1.f;
    canvas.SetCamera(camera);
    canvas.SetLighting(DirectionalLight{}, AmbientLight{});

    // Un matériau par face du Cube (6 groupes, voir Mesh::Cube()) — vérifie
    // le trajet Shape::MaterialFor()/Mesh::Group bout en bout.
    std::vector<Material> perFaceMaterials = {
        Material::Plastic(sdl3::Color::RED()), Material::Plastic(sdl3::Color::GREEN()),
        Material::Plastic(sdl3::Color::BLUE()), Material::Unlit(sdl3::Color::WHITE()),
        Material::Metal(sdl3::Color::WHITE()),  Material::Wood(),
    };
    auto root = std::make_unique<Shape>(Mesh::Cube(), perFaceMaterials);

    auto childOwned = std::make_unique<Shape>(Mesh::Sphere(0.5f, 12, 8), Material::Default());
    childOwned->SetPosition({2.f, 0.f, 0.f});
    root->Add(std::move(childOwned));

    bool began = canvas.Begin();
    if (began) {
        canvas.DrawObject(*root);
        canvas.End();
    }

    EXPECT_TRUE(sdl3::GetError().IsEmpty());
}

TEST(Render3dCanvas, PbrMaterialThroughShapeEndToEnd) {
    auto ctx = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
    ASSERT_TRUE(ctx.IsOk());

    auto windowResult = sdl3::Window::Create(String("render3d_smoke_test_pbr"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    sdl3::Window window = std::move(windowResult.Value());

    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());

    Camera camera;
    camera.position = {0.f, 0.f, -5.f};
    camera.aspect = 1.f;
    canvas.SetCamera(camera);
    canvas.SetLighting(DirectionalLight{}, AmbientLight{});

    // Material::Pbr() sans lumière ponctuelle/spot active : exerce le chemin
    // "PBR sans multiLight" de GetOrCreatePipeline (needsBuiltShader forcé
    // par key.shader == PBR à lui seul, voir canvas.hpp).
    auto root = std::make_unique<Shape>(Mesh::TorusKnot(1.f, 0.3f, 32, 6, 2, 3),
                                        Material::Pbr(sdl3::Color::YELLOW(), 0.8f, 0.3f));

    bool began = canvas.Begin();
    if (began) {
        canvas.DrawObject(*root);
        canvas.End();
    }

    EXPECT_TRUE(sdl3::GetError().IsEmpty());
}

// ── Points / LineSegments (M15) ─────────────────────────────────────────────

TEST(Render3dMesh, FromPointsAndFromLineSegmentsVertexCounts) {
    math::FVector3 points[4] = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
    Mesh cloud = Mesh::FromPoints(points, sdl3::Color::WHITE());
    EXPECT_TRUE(cloud.Vertices().size() == 4);
    EXPECT_TRUE(cloud.IndexCount() == 4);
    // Indices identité (0,1,2,3) : chaque sommet dessiné une fois, aucune
    // réutilisation (voir le commentaire de FromPoints(), mesh.hpp).
    for (uint32_t i = 0; i < 4; ++i)
        EXPECT_TRUE(cloud.Indices()[i] == i);

    math::FVector3 segmentPoints[4] = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
    Mesh lines = Mesh::FromLineSegments(segmentPoints, sdl3::Color::WHITE());
    EXPECT_TRUE(lines.Vertices().size() == 4);
    EXPECT_TRUE(lines.IndexCount() == 4); // 2 segments indépendants (paires 0-1, 2-3)
}

TEST(Render3dCanvas, PointsAndLineSegmentsDrawEndToEnd) {
    auto ctx = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
    ASSERT_TRUE(ctx.IsOk());

    auto windowResult = sdl3::Window::Create(String("render3d_smoke_test_points"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    sdl3::Window window = std::move(windowResult.Value());

    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());

    Camera camera;
    camera.position = {0.f, 0.f, -5.f};
    camera.aspect = 1.f;
    canvas.SetCamera(camera);
    canvas.SetLighting(DirectionalLight{}, AmbientLight{});

    math::FVector3 pointPositions[3] = {{-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}};
    Points pointCloud(Mesh::FromPoints(pointPositions, sdl3::Color::RED()), Material::Unlit(sdl3::Color::RED()));

    math::FVector3 linePositions[4] = {{-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {-1.f, 1.f, 0.f}, {1.f, 1.f, 0.f}};
    LineSegments lines(Mesh::FromLineSegments(linePositions, sdl3::Color::BLUE()),
                       Material::Unlit(sdl3::Color::BLUE()));

    bool began = canvas.Begin();
    if (began) {
        canvas.DrawObject(pointCloud);
        canvas.DrawObject(lines);
        canvas.End();
    }

    EXPECT_TRUE(sdl3::GetError().IsEmpty());
}

// ── Sprite / InstancedMesh (M16) ────────────────────────────────────────────

TEST(Render3dObject3D, SpriteBillboardFacesCamera) {
    // CPU pur (voir Sprite::ComputeBillboardRotation, extraite d'OnDraw()
    // exactement pour permettre ce test sans device GPU) : après rotation,
    // l'axe local +Z du sprite doit pointer vers la caméra, et l'axe local
    // +Y doit rester aligné avec camera.up (à la projection près sur le plan
    // perpendiculaire à la direction de vue).
    Camera camera;
    camera.position = {3.f, 2.f, 4.f};
    camera.target = {0.f, 0.f, 0.f};
    camera.up = {0.f, 1.f, 0.f};

    math::FVector3 spritePosition{0.f, 0.f, 0.f};
    math::FQuaternion rotation = Sprite::ComputeBillboardRotation(spritePosition, camera);

    math::FVector3 expectedForward = (camera.position - spritePosition).Normalize();
    math::FVector3 actualForward = rotation.Rotate({0.f, 0.f, 1.f});
    EXPECT_TRUE(actualForward.Distance(expectedForward) < 1e-4f);

    // L'up local tourné doit être perpendiculaire à actualForward (c'est la
    // définition même du "roulis corrigé") et du même côté que camera.up
    // (produit scalaire positif) plutôt qu'inversé.
    math::FVector3 actualUp = rotation.Rotate({0.f, 1.f, 0.f});
    EXPECT_TRUE(std::abs(actualUp.Dot(actualForward)) < 1e-4f);
    EXPECT_TRUE(actualUp.Dot(camera.up) > 0.f);
}

TEST(Render3dCanvas, InstancedMeshDrawsMultipleDistinctInstances) {
    auto ctx = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
    ASSERT_TRUE(ctx.IsOk());

    auto windowResult = sdl3::Window::Create(String("render3d_smoke_test_instanced"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    sdl3::Window window = std::move(windowResult.Value());

    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());

    Camera camera;
    camera.position = {0.f, 0.f, -5.f};
    camera.aspect = 1.f;
    canvas.SetCamera(camera);
    canvas.SetLighting(DirectionalLight{}, AmbientLight{});

    InstancedMesh instanced(Mesh::Sphere(0.3f, 8, 6), Material::Unlit(sdl3::Color::WHITE()));
    instanced.Instances() = {
        math::FMatrix4::Translate(-2.f, 0.f, 0.f),
        math::FMatrix4::Translate(2.f, 0.f, 0.f),
    };

    bool began = canvas.Begin();
    if (began) {
        canvas.DrawObject(instanced);
        canvas.End();
    }

    EXPECT_TRUE(sdl3::GetError().IsEmpty());
    EXPECT_TRUE(instanced.Geometry().HasGpuBuffers() || !began);
}

TEST(Render3dCanvas, InstancedMeshPixelReadbackShowsTwoDistinctInstances) {
    // Vérification par pixel réelle (le Canvas plein n'est pas fiable en
    // environnement sandboxé sans compositeur, voir les autres tests bas
    // niveau du dépôt) : pipeline instancié construit directement via
    // ShaderBuilder().Instanced(true), un petit quad dessiné 2 fois à des
    // positions distinctes (instance 0 à x=-0.5, instance 1 à x=+0.5) — un
    // seul DrawIndexedPrimitives(count, numInstances=2, 0). Vue-projection
    // identité (quad dans le plan XY, Z=0 constant -> NDC.xy = position.xy
    // directement, voir le commentaire équivalent de shader_builder_smoke_test.cpp).
    constexpr uint32_t SIZE = 64;

    auto windowResult = sdl3::Window::Create(String("render3d_smoke_test_instanced_px"), int(SIZE), int(SIZE), 0);
    ASSERT_TRUE(windowResult.IsOk());
    sdl3::Window window = std::move(windowResult.Value());
    auto deviceResult = sdl3::GpuDevice::Create(sdl3::gpu_shader_format::SPIR_V | sdl3::gpu_shader_format::MSL |
                                                sdl3::gpu_shader_format::DXIL);
    ASSERT_TRUE(deviceResult.IsOk());
    sdl3::GpuDevice device = std::move(deviceResult.Value());
    ASSERT_TRUE(device.ClaimWindow(MakeRef(window)));

    auto programResult =
        ShaderBuilder().Color(sdl3::FColor{1.f, 1.f, 1.f, 1.f}).Lit(LightingModel::UNLIT).Instanced(true).Build(device);
    ASSERT_TRUE(programResult.IsOk());
    ShaderProgram program = std::move(programResult.Value());

    sdl3::GpuVertexBufferDescription vertexBufferDescs[2] = {
        {0, sizeof(Vertex3D), sdl3::gpu_vertex_input_rate::VERTEX, 0},
        {1, sizeof(math::FMatrix4), sdl3::gpu_vertex_input_rate::INSTANCE, 0},
    };
    sdl3::GpuVertexAttribute vertexAttributes[8] = {
        {0, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(Vertex3D, position))},
        {1, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(Vertex3D, normal))},
        {2, 0, sdl3::gpu_vertex_element_format::FLOAT2, uint32_t(offsetof(Vertex3D, uv))},
        {3, 0, sdl3::gpu_vertex_element_format::UBYTE4_NORM, uint32_t(offsetof(Vertex3D, color))},
        {4, 1, sdl3::gpu_vertex_element_format::FLOAT4, 0},
        {5, 1, sdl3::gpu_vertex_element_format::FLOAT4, 16},
        {6, 1, sdl3::gpu_vertex_element_format::FLOAT4, 32},
        {7, 1, sdl3::gpu_vertex_element_format::FLOAT4, 48},
    };
    sdl3::GpuColorTargetDescription colorTargetDesc{};
    colorTargetDesc.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    sdl3::GpuGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = program.Vertex().Get();
    pipelineInfo.fragment_shader = program.Fragment().Get();
    pipelineInfo.vertex_input_state.vertex_buffer_descriptions = vertexBufferDescs;
    pipelineInfo.vertex_input_state.num_vertex_buffers = 2;
    pipelineInfo.vertex_input_state.vertex_attributes = vertexAttributes;
    pipelineInfo.vertex_input_state.num_vertex_attributes = 8;
    pipelineInfo.primitive_type = sdl3::gpu_primitive_type::TRIANGLE_LIST;
    pipelineInfo.rasterizer_state.fill_mode = sdl3::gpu_fill_mode::FILL;
    pipelineInfo.rasterizer_state.cull_mode = sdl3::gpu_cull_mode::NONE;
    pipelineInfo.rasterizer_state.front_face = sdl3::gpu_front_face::CLOCKWISE;
    pipelineInfo.multisample_state.sample_count = sdl3::gpu_sample_count::SAMPLE1;
    pipelineInfo.depth_stencil_state.enable_depth_test = false;
    pipelineInfo.depth_stencil_state.enable_depth_write = false;
    pipelineInfo.target_info.color_target_descriptions = &colorTargetDesc;
    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.has_depth_stencil_target = false;
    auto pipelineResult = device.CreateGraphicsPipeline(pipelineInfo);
    ASSERT_TRUE(pipelineResult.IsOk());
    sdl3::GpuGraphicsPipeline pipeline = std::move(pipelineResult.Value());

    // Petit quad (0.4x0.4) centré à l'origine locale.
    Vertex3D quad[6] = {
        {{-0.2f, -0.2f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}, sdl3::Color::WHITE()},
        {{0.2f, -0.2f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}, sdl3::Color::WHITE()},
        {{0.2f, 0.2f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f}, sdl3::Color::WHITE()},
        {{-0.2f, -0.2f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}, sdl3::Color::WHITE()},
        {{0.2f, 0.2f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f}, sdl3::Color::WHITE()},
        {{-0.2f, 0.2f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f}, sdl3::Color::WHITE()},
    };
    auto vbResult = device.CreateBuffer(sdl3::gpu_buffer_usage::VERTEX, uint32_t(sizeof(quad)));
    ASSERT_TRUE(vbResult.IsOk());
    sdl3::GpuBuffer vertexBuffer = std::move(vbResult.Value());
    {
        auto tbResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, uint32_t(sizeof(quad)));
        ASSERT_TRUE(tbResult.IsOk());
        sdl3::GpuTransferBuffer transfer = std::move(tbResult.Value());
        {
            auto mapped = transfer.Map(false);
            ASSERT_TRUE(bool(mapped));
            std::memcpy(mapped.GetData(), quad, sizeof(quad));
        }
        auto uploadCmd = device.AcquireCommandBuffer();
        sdl3::GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        sdl3::GpuTransferBufferLocation src{transfer.Get(), 0};
        sdl3::GpuBufferRegion dst{vertexBuffer.Get(), 0, uint32_t(sizeof(quad))};
        copyPass.UploadToBuffer(src, dst, false);
        ASSERT_TRUE(uploadCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
    }

    math::FMatrix4 instances[2] = {math::FMatrix4::Translate(-0.5f, 0.f, 0.f), math::FMatrix4::Translate(0.5f, 0.f, 0.f)};
    auto ibResult = device.CreateBuffer(sdl3::gpu_buffer_usage::VERTEX, uint32_t(sizeof(instances)));
    ASSERT_TRUE(ibResult.IsOk());
    sdl3::GpuBuffer instanceBuffer = std::move(ibResult.Value());
    {
        auto tbResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, uint32_t(sizeof(instances)));
        ASSERT_TRUE(tbResult.IsOk());
        sdl3::GpuTransferBuffer transfer = std::move(tbResult.Value());
        {
            auto mapped = transfer.Map(false);
            ASSERT_TRUE(bool(mapped));
            std::memcpy(mapped.GetData(), instances, sizeof(instances));
        }
        auto uploadCmd = device.AcquireCommandBuffer();
        sdl3::GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        sdl3::GpuTransferBufferLocation src{transfer.Get(), 0};
        sdl3::GpuBufferRegion dst{instanceBuffer.Get(), 0, uint32_t(sizeof(instances))};
        copyPass.UploadToBuffer(src, dst, false);
        ASSERT_TRUE(uploadCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
    }

    sdl3::GpuTextureCreateInfo colorInfo{};
    colorInfo.type = sdl3::gpu_texture_type::TEXTURE2_D;
    colorInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    colorInfo.usage = sdl3::gpu_texture_usage::COLOR_TARGET | sdl3::gpu_texture_usage::SAMPLER;
    colorInfo.width = SIZE;
    colorInfo.height = SIZE;
    colorInfo.layer_count_or_depth = 1;
    colorInfo.num_levels = 1;
    colorInfo.sample_count = sdl3::gpu_sample_count::SAMPLE1;
    auto colorResult = device.CreateTexture(colorInfo);
    ASSERT_TRUE(colorResult.IsOk());
    sdl3::GpuTexture colorTexture = std::move(colorResult.Value());

    struct MaterialUBO {
        float baseColor[4];
        float emissive[4];
        float roughness, metallic, useTexture, useIBL;
    };
    MaterialUBO material{{1.f, 1.f, 1.f, 1.f}, {0.f, 0.f, 0.f, 0.f}, 0.f, 0.f, 0.f, 0.f};

    auto frameCmd = device.AcquireCommandBuffer();
    sdl3::GpuColorTargetInfo colorTarget{};
    colorTarget.texture = colorTexture.Get();
    colorTarget.clear_color = SDL_FColor{0.f, 0.f, 0.f, 1.f};
    colorTarget.load_op = sdl3::gpu_load_op::CLEAR;
    colorTarget.store_op = sdl3::gpu_store_op::STORE;
    {
        sdl3::GpuRenderPass pass = frameCmd.BeginRenderPass(colorTarget, nullptr);
        pass.BindPipeline(pipeline);
        sdl3::GpuBufferBinding vertexBinding{vertexBuffer.Get(), 0};
        pass.BindVertexBuffer(0, vertexBinding);
        sdl3::GpuBufferBinding instanceBinding{instanceBuffer.Get(), 0};
        pass.BindVertexBuffer(1, instanceBinding);
        // uViewProjection (utilisé même en mode instancié) + PerObjectUBO
        // (déclaré mais ignoré par le vertex shader instancié, voir
        // shader_chunks::VERTEX_MAIN_UNLIT — une valeur bidon suffit).
        frameCmd.PushVertexUniformData(0, math::FMatrix4::Identity());
        struct PerObjectUBO {
            math::FMatrix4 model;
            math::FMatrix4 normalMatrix;
        };
        frameCmd.PushVertexUniformData(1, PerObjectUBO{math::FMatrix4::Identity(), math::FMatrix4::Identity()});
        frameCmd.PushFragmentUniformData(0, material);
        pass.DrawPrimitives(6, 2, 0, 0);
    }
    ASSERT_TRUE(frameCmd.Submit());
    ASSERT_TRUE(device.WaitIdle());

    auto downloadResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::DOWNLOAD, SIZE * SIZE * 4);
    ASSERT_TRUE(downloadResult.IsOk());
    sdl3::GpuTransferBuffer downloadBuffer = std::move(downloadResult.Value());
    auto downloadCmd = device.AcquireCommandBuffer();
    {
        sdl3::GpuCopyPass copyPass = downloadCmd.BeginCopyPass();
        sdl3::GpuTextureRegion src{colorTexture.Get(), 0, 0, 0, 0, 0, SIZE, SIZE, 1};
        sdl3::GpuTextureTransferInfo dst{downloadBuffer.Get(), 0, SIZE, SIZE};
        copyPass.DownloadFromTexture(src, dst);
    }
    ASSERT_TRUE(downloadCmd.Submit());
    ASSERT_TRUE(device.WaitIdle());

    auto mapped = downloadBuffer.Map(false);
    ASSERT_TRUE(bool(mapped));
    uint8_t *pixels = mapped.As<uint8_t>();
    // NDC x=-0.5 -> pixel x=16 ; NDC x=0.5 -> pixel x=48 ; NDC x=0 (entre les
    // deux quads, ne devrait toucher aucune instance) -> pixel x=32 — voir
    // le calcul en tête de test.
    auto brightnessAt = [&](int px, int py) { return int(pixels[(py * int(SIZE) + px) * 4 + 0]); };
    int leftInstance = brightnessAt(16, 32);
    int rightInstance = brightnessAt(48, 32);
    int betweenInstances = brightnessAt(32, 32);
    EXPECT_TRUE(leftInstance > 200);
    EXPECT_TRUE(rightInstance > 200);
    EXPECT_TRUE(betweenInstances < 50);

    EXPECT_TRUE(sdl3::GetError().IsEmpty());
}

// ── SkinnedMesh / Bone (M17) ────────────────────────────────────────────────

TEST(Render3dCanvas, SkinnedMeshBonePoseDeformsOnlyWeightedHalf) {
    // Vérification par pixel réelle (Canvas plein non fiable en sandbox, voir
    // les autres tests bas niveau du dépôt) : deux quads, l'un pesé 100% sur
    // l'os racine (jamais posé manuellement), l'autre 100% sur un os enfant
    // (posé à 180° autour de Z) — seul le second doit visiblement bouger.
    // Vue-projection identité (quads dans le plan XY, Z=0 constant -> NDC.xy
    // = position.xy directement, voir le commentaire équivalent de
    // shader_builder_smoke_test.cpp).
    constexpr uint32_t SIZE = 64;

    auto windowResult = sdl3::Window::Create(String("render3d_smoke_test_skinned"), int(SIZE), int(SIZE), 0);
    ASSERT_TRUE(windowResult.IsOk());
    sdl3::Window window = std::move(windowResult.Value());
    auto deviceResult = sdl3::GpuDevice::Create(sdl3::gpu_shader_format::SPIR_V | sdl3::gpu_shader_format::MSL |
                                                sdl3::gpu_shader_format::DXIL);
    ASSERT_TRUE(deviceResult.IsOk());
    sdl3::GpuDevice device = std::move(deviceResult.Value());
    ASSERT_TRUE(device.ClaimWindow(MakeRef(window)));

    auto programResult =
        ShaderBuilder().Color(sdl3::FColor{1.f, 1.f, 1.f, 1.f}).Lit(LightingModel::UNLIT).Skinned(true).Build(device);
    ASSERT_TRUE(programResult.IsOk());
    ShaderProgram program = std::move(programResult.Value());

    sdl3::GpuVertexBufferDescription vertexBufferDesc{0, sizeof(SkinnedVertex3D), sdl3::gpu_vertex_input_rate::VERTEX,
                                                       0};
    sdl3::GpuVertexAttribute vertexAttributes[6] = {
        {0, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(SkinnedVertex3D, position))},
        {1, 0, sdl3::gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(SkinnedVertex3D, normal))},
        {2, 0, sdl3::gpu_vertex_element_format::FLOAT2, uint32_t(offsetof(SkinnedVertex3D, uv))},
        {3, 0, sdl3::gpu_vertex_element_format::UBYTE4_NORM, uint32_t(offsetof(SkinnedVertex3D, color))},
        {4, 0, sdl3::gpu_vertex_element_format::FLOAT4, uint32_t(offsetof(SkinnedVertex3D, boneIndices))},
        {5, 0, sdl3::gpu_vertex_element_format::FLOAT4, uint32_t(offsetof(SkinnedVertex3D, boneWeights))},
    };
    sdl3::GpuColorTargetDescription colorTargetDesc{};
    colorTargetDesc.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    sdl3::GpuGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = program.Vertex().Get();
    pipelineInfo.fragment_shader = program.Fragment().Get();
    pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vertexBufferDesc;
    pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
    pipelineInfo.vertex_input_state.vertex_attributes = vertexAttributes;
    pipelineInfo.vertex_input_state.num_vertex_attributes = 6;
    pipelineInfo.primitive_type = sdl3::gpu_primitive_type::TRIANGLE_LIST;
    pipelineInfo.rasterizer_state.fill_mode = sdl3::gpu_fill_mode::FILL;
    pipelineInfo.rasterizer_state.cull_mode = sdl3::gpu_cull_mode::NONE;
    pipelineInfo.rasterizer_state.front_face = sdl3::gpu_front_face::CLOCKWISE;
    pipelineInfo.multisample_state.sample_count = sdl3::gpu_sample_count::SAMPLE1;
    pipelineInfo.depth_stencil_state.enable_depth_test = false;
    pipelineInfo.depth_stencil_state.enable_depth_write = false;
    pipelineInfo.target_info.color_target_descriptions = &colorTargetDesc;
    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.has_depth_stencil_target = false;
    auto pipelineResult = device.CreateGraphicsPipeline(pipelineInfo);
    ASSERT_TRUE(pipelineResult.IsOk());
    sdl3::GpuGraphicsPipeline pipeline = std::move(pipelineResult.Value());

    // Bas (bone 0 = racine, jamais posé) : y in [-0.5,0], rouge.
    // Haut (bone 1 = enfant, pivot (0,0.5,0)) : y in [0.5,1.0], bleu.
    SkinnedVertex3D bottom[6];
    SkinnedVertex3D top[6];
    {
        math::FVector2 corners[6] = {{-0.3f, -0.5f}, {0.3f, -0.5f}, {0.3f, 0.f},
                                     {-0.3f, -0.5f}, {0.3f, 0.f},  {-0.3f, 0.f}};
        for (int i = 0; i < 6; ++i)
            bottom[i] = SkinnedVertex3D{{corners[i].x, corners[i].y, 0.f},
                                        {0.f, 0.f, 1.f},
                                        {0.f, 0.f},
                                        sdl3::Color::RED(),
                                        {0.f, 0.f, 0.f, 0.f},
                                        {1.f, 0.f, 0.f, 0.f}};
    }
    {
        math::FVector2 corners[6] = {{-0.3f, 0.5f}, {0.3f, 0.5f}, {0.3f, 1.f},
                                     {-0.3f, 0.5f}, {0.3f, 1.f},  {-0.3f, 1.f}};
        for (int i = 0; i < 6; ++i)
            top[i] = SkinnedVertex3D{{corners[i].x, corners[i].y, 0.f},
                                     {0.f, 0.f, 1.f},
                                     {0.f, 0.f},
                                     sdl3::Color::BLUE(),
                                     {1.f, 0.f, 0.f, 0.f}, // bone 1 = enfant
                                     {1.f, 0.f, 0.f, 0.f}};
    }
    std::vector<SkinnedVertex3D> vertices;
    vertices.insert(vertices.end(), std::begin(bottom), std::end(bottom));
    vertices.insert(vertices.end(), std::begin(top), std::end(top));
    std::vector<uint32_t> indices(12);
    for (uint32_t i = 0; i < 12; ++i)
        indices[i] = i;

    auto rootBone = std::make_unique<Bone>();
    auto childBonePtr = std::make_unique<Bone>();
    childBonePtr->SetPosition({0.f, 0.5f, 0.f});
    childBonePtr->SetName(String("childBone")); // ciblé par nom depuis le clip d'animation ci-dessous
    auto &childBoneRef = static_cast<Bone &>(rootBone->Add(std::move(childBonePtr)));
    Bone *rootBonePtr = rootBone.get();
    Bone *childBone = &childBoneRef;
    std::vector<Bone *> bones = {rootBonePtr, childBone};

    SkinnedGeometry geometry(vertices, indices);
    SkinnedMesh skinned(std::move(geometry), Material::Unlit(sdl3::Color::WHITE()), std::move(rootBone), bones);

    // Upload de la géométrie une seule fois (les positions ne changent pas —
    // seule la pose des os change entre les deux rendus ci-dessous).
    {
        auto uploadCmd = device.AcquireCommandBuffer();
        auto uploaded = skinned.Geometry().EnsureGpuBuffers(device, uploadCmd);
        ASSERT_TRUE(uploaded.IsOk());
        ASSERT_TRUE(uploadCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
    }

    struct MaterialUBO {
        float baseColor[4];
        float emissive[4];
        float roughness, metallic, useTexture, useIBL;
    };
    MaterialUBO material{{1.f, 1.f, 1.f, 1.f}, {0.f, 0.f, 0.f, 0.f}, 0.f, 0.f, 0.f, 0.f};

    // Rend l'état courant du squelette dans une texture fraîche et retourne
    // (r,g,b) du pixel demandé — pas de lambda ici (portage direct du
    // corps, évite toute capture/ownership ambiguë sur les objets GPU).
    auto renderAndReadPixel = [&](int px, int py, uint8_t out[3]) {
        auto skinMatrices = skinned.ComputeSkinMatrices();
        uint32_t boneBytes = uint32_t(skinMatrices.size() * sizeof(math::FMatrix4));
        auto boneBufferResult = device.CreateBuffer(sdl3::gpu_buffer_usage::GRAPHICS_STORAGE_READ, boneBytes);
        ASSERT_TRUE(boneBufferResult.IsOk());
        sdl3::GpuBuffer boneBuffer = std::move(boneBufferResult.Value());
        {
            auto tbResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, boneBytes);
            ASSERT_TRUE(tbResult.IsOk());
            sdl3::GpuTransferBuffer transfer = std::move(tbResult.Value());
            {
                auto mapped = transfer.Map(false);
                ASSERT_TRUE(bool(mapped));
                std::memcpy(mapped.GetData(), skinMatrices.data(), boneBytes);
            }
            auto uploadCmd = device.AcquireCommandBuffer();
            sdl3::GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
            sdl3::GpuTransferBufferLocation src{transfer.Get(), 0};
            sdl3::GpuBufferRegion dst{boneBuffer.Get(), 0, boneBytes};
            copyPass.UploadToBuffer(src, dst, false);
            ASSERT_TRUE(uploadCmd.Submit());
            ASSERT_TRUE(device.WaitIdle());
        }

        sdl3::GpuTextureCreateInfo colorInfo{};
        colorInfo.type = sdl3::gpu_texture_type::TEXTURE2_D;
        colorInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        colorInfo.usage = sdl3::gpu_texture_usage::COLOR_TARGET | sdl3::gpu_texture_usage::SAMPLER;
        colorInfo.width = SIZE;
        colorInfo.height = SIZE;
        colorInfo.layer_count_or_depth = 1;
        colorInfo.num_levels = 1;
        colorInfo.sample_count = sdl3::gpu_sample_count::SAMPLE1;
        auto colorResult = device.CreateTexture(colorInfo);
        ASSERT_TRUE(colorResult.IsOk());
        sdl3::GpuTexture colorTexture = std::move(colorResult.Value());

        auto frameCmd = device.AcquireCommandBuffer();
        sdl3::GpuColorTargetInfo colorTarget{};
        colorTarget.texture = colorTexture.Get();
        colorTarget.clear_color = SDL_FColor{0.f, 0.f, 0.f, 1.f};
        colorTarget.load_op = sdl3::gpu_load_op::CLEAR;
        colorTarget.store_op = sdl3::gpu_store_op::STORE;
        {
            sdl3::GpuRenderPass pass = frameCmd.BeginRenderPass(colorTarget, nullptr);
            pass.BindPipeline(pipeline);
            sdl3::GpuBufferBinding vertexBinding{skinned.Geometry().VertexBuffer()->Get(), 0};
            pass.BindVertexBuffer(0, vertexBinding);
            sdl3::GpuBufferBinding indexBinding{skinned.Geometry().IndexBuffer()->Get(), 0};
            pass.BindIndexBuffer(indexBinding, sdl3::gpu_index_element_size::BITS32);
            Ref<sdl3::GpuBuffer> boneBufferRef = MakeRef(boneBuffer);
            pass.BindVertexStorageBuffers(0, {&boneBufferRef, 1});
            frameCmd.PushVertexUniformData(0, math::FMatrix4::Identity());
            struct PerObjectUBO {
                math::FMatrix4 model;
                math::FMatrix4 normalMatrix;
            };
            frameCmd.PushVertexUniformData(1, PerObjectUBO{math::FMatrix4::Identity(), math::FMatrix4::Identity()});
            frameCmd.PushFragmentUniformData(0, material);
            pass.DrawIndexedPrimitives(skinned.Geometry().IndexCount(), 1, 0);
        }
        ASSERT_TRUE(frameCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());

        auto downloadResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::DOWNLOAD, SIZE * SIZE * 4);
        ASSERT_TRUE(downloadResult.IsOk());
        sdl3::GpuTransferBuffer downloadBuffer = std::move(downloadResult.Value());
        auto downloadCmd = device.AcquireCommandBuffer();
        {
            sdl3::GpuCopyPass copyPass = downloadCmd.BeginCopyPass();
            sdl3::GpuTextureRegion src{colorTexture.Get(), 0, 0, 0, 0, 0, SIZE, SIZE, 1};
            sdl3::GpuTextureTransferInfo dst{downloadBuffer.Get(), 0, SIZE, SIZE};
            copyPass.DownloadFromTexture(src, dst);
        }
        ASSERT_TRUE(downloadCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());

        auto mapped = downloadBuffer.Map(false);
        ASSERT_TRUE(bool(mapped));
        uint8_t *pixels = mapped.As<uint8_t>();
        size_t idx = (size_t(py) * SIZE + size_t(px)) * 4;
        out[0] = pixels[idx + 0];
        out[1] = pixels[idx + 1];
        out[2] = pixels[idx + 2];
    };

    // Points sondés (NDC -> pixel = (ndc+1)/2*SIZE) : haut du quad bleu
    // (ndc y=0.75 -> pixel y = (1-0.75)/... voir la conversion ci-dessous),
    // bas du quad rouge. NDC.y=0.75 correspond au pixel row (1-0.75)/2*SIZE
    // ou ((0.75+1)/2)*SIZE selon le sens Y écran/NDC — les deux sont testés
    // implicitement par construction : peu importe le sens exact, seul
    // compte que le PIXEL COURANT AU MÊME ENDROIT change entre les deux
    // rendus pour le point "haut" et reste stable pour le point "bas".
    int topPx = int(SIZE) / 2, topPy = int((1.f - 0.75f) / 2.f * float(SIZE));
    int bottomPx = int(SIZE) / 2, bottomPy = int((1.f - (-0.25f)) / 2.f * float(SIZE));

    uint8_t beforeTop[3], beforeBottom[3];
    renderAndReadPixel(topPx, topPy, beforeTop);
    renderAndReadPixel(bottomPx, bottomPy, beforeBottom);
    // Avant toute pose : le point haut doit être bleu (quad du haut présent),
    // le point bas rouge (quad du bas présent) — sanity check de la scène.
    EXPECT_TRUE(beforeTop[2] > 200 && beforeTop[0] < 50);    // bleu
    EXPECT_TRUE(beforeBottom[0] > 200 && beforeBottom[2] < 50); // rouge

    // Pose via un clip d'animation (M27) plutôt qu'un SetRotation() manuel :
    // 180° autour de Z sur l'os ENFANT seulement — le quad bleu (pesé sur cet
    // os) doit bouger, le quad rouge (pesé sur la racine, jamais posée) doit
    // rester identique. Clip à 2 keyframes (identité -> 180° autour de Z)
    // ciblant l'os enfant par nom, joué jusqu'à t=1.0 via
    // AnimationMixer::Update() : vérifie que le système d'animation
    // générique produit exactement la même pose que l'ancien appel manuel
    // (mêmes assertions de pixels avant/après, inchangées ci-dessous).
    AnimationClip pose;
    pose.name = "childBonePose";
    KeyframeTrack<math::FQuaternion> rotationTrack;
    rotationTrack.targetName = "childBone";
    rotationTrack.keyframes = {{0.f, math::FQuaternion::Identity()},
                                {1.f, math::FQuaternion::FromAxisAngle({0.f, 0.f, 1.f}, 3.14159265f)}};
    pose.rotationTracks.push_back(rotationTrack);

    AnimationMixer mixer(skinned);
    mixer.Play(pose, /*loop=*/false);
    mixer.Update(1.f); // avance jusqu'à t=1.0 -> pose finale identique à l'ancien SetRotation()

    uint8_t afterTop[3], afterBottom[3];
    renderAndReadPixel(topPx, topPy, afterTop);
    renderAndReadPixel(bottomPx, bottomPy, afterBottom);

    // Le point "haut" n'est plus bleu (le quad bleu a pivoté ailleurs).
    EXPECT_TRUE(!(afterTop[2] > 200 && afterTop[0] < 50));
    // Le point "bas" est resté rouge (os racine non affecté).
    EXPECT_TRUE(afterBottom[0] > 200 && afterBottom[2] < 50);

    EXPECT_TRUE(sdl3::GetError().IsEmpty());
}

// ── LOD (M18) ────────────────────────────────────────────────────────────

TEST(Render3dObject3D, LodSelectsCorrectLevelByCameraDistance) {
    // Essentiellement CPU pur (voir lod.hpp : OnDraw() ne dessine rien lui-
    // même, il bascule juste la visibilité des niveaux) — un device GPU réel
    // reste nécessaire pour construire un Canvas (canvas.GetCamera() est la
    // seule chose utilisée par LOD::OnDraw), mais aucun draw/frame n'est
    // soumis, voir le plan ("sans avoir besoin d'un rendu GPU").
    auto ctx = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
    ASSERT_TRUE(ctx.IsOk());
    auto windowResult = sdl3::Window::Create(String("render3d_smoke_test_lod"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    sdl3::Window window = std::move(windowResult.Value());
    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());

    LOD lod;
    Object3D &near = lod.AddLevel(0.f, std::make_unique<Object3D>());
    Object3D &mid = lod.AddLevel(10.f, std::make_unique<Object3D>());
    Object3D &far = lod.AddLevel(30.f, std::make_unique<Object3D>());

    auto expectVisible = [&](Object3D &expected) {
        EXPECT_TRUE(expected.IsVisible());
        for (Object3D *other : {&near, &mid, &far})
            if (other != &expected)
                EXPECT_TRUE(!other->IsVisible());
    };

    Camera camera;
    camera.target = {0.f, 0.f, 0.f};

    camera.position = {5.f, 0.f, 0.f}; // distance=5 : entre 0 et 10 -> "near"
    canvas.SetCamera(camera);
    lod.OnDraw(canvas);
    expectVisible(near);

    camera.position = {15.f, 0.f, 0.f}; // distance=15 : entre 10 et 30 -> "mid"
    canvas.SetCamera(camera);
    lod.OnDraw(canvas);
    expectVisible(mid);

    camera.position = {100.f, 0.f, 0.f}; // distance=100 : au-delà de 30 -> "far"
    canvas.SetCamera(camera);
    lod.OnDraw(canvas);
    expectVisible(far);

    camera.position = {0.f, 0.f, 0.f}; // distance=0 : exactement le seuil "near"
    canvas.SetCamera(camera);
    lod.OnDraw(canvas);
    expectVisible(near);
}

int main() {
    return RUN_ALL_TESTS();
}
