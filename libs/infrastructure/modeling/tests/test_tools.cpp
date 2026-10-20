#include "test.hpp"
#include <modeling/io.hpp>
#include <modeling/operations.hpp>
#include <modeling/tools.hpp>

#include <cmath>

using namespace modeling;

namespace {

/// Caméra perspective de test : projection en pixels et rayons de la souris.
struct TestCamera {
	math::FMatrix4 viewProj, inverse;
	float width = 800.f, height = 600.f;
	Vec3 eye;

	TestCamera(const Vec3 &eyeIn, const Vec3 &target) : eye(eyeIn) {
		viewProj = math::FMatrix4::Perspective(60.f * math::PI / 180.f, width / height, 0.1f, 100.f) *
				   math::FMatrix4::LookAt(eye, target, {0.f, 1.f, 0.f});
		inverse = viewProj.Inverse();
	}
	Option<math::FVector2> Project(const Vec3 &p) const {
		if ((p - eye).Dot(Forward()) <= 0.f)
			return NONE;
		const Vec3 ndc = viewProj.TransformPoint(p);
		return Some(math::FVector2{(ndc.x + 1.f) * 0.5f * width, (1.f - ndc.y) * 0.5f * height});
	}
	Vec3 Forward() const {
		const Vec3 a = inverse.TransformPoint({0.f, 0.f, 0.f}), b = inverse.TransformPoint({0.f, 0.f, 1.f});
		return (b - a).Normalize();
	}
	math::FRay Ray(const math::FVector2 &px) const {
		const float x = px.x / width * 2.f - 1.f, y = 1.f - px.y / height * 2.f;
		const Vec3 a = inverse.TransformPoint({x, y, 0.f}), b = inverse.TransformPoint({x, y, 1.f});
		return math::FRay(a, b - a);
	}
	Projector Projector_() const {
		return [this](const Vec3 &p) { return Project(p); };
	}
};

/// Un utilisateur qui manipule un outil à la souris.
struct User {
	Model model;
	Selection selection;
	TestCamera camera{{6.f, 7.f, 9.f}, {1.f, 0.f, 1.f}};
	std::unique_ptr<Tool> tool;
	uint32_t material = INVALID_ID;

	ToolContext Ctx() { return ToolContext{model, selection, material}; }
	ToolEvent At(const math::FVector2 &px, bool ctrl = false) {
		InferenceQuery q{camera.Ray(px), px, camera.Projector_(), tool ? tool->Anchor() : Option<Vec3>(NONE)};
		ToolEvent e{Infer(model, q), q.ray, false, ctrl};
		return e;
	}
	math::FVector2 Px(const Vec3 &p) const { return camera.Project(p).Unwrap(); }
	ToolResult Click(const Vec3 &p, bool ctrl = false) {
		ToolContext ctx = Ctx();
		const ToolEvent e = At(Px(p), ctrl);
		tool->Hover(ctx, e);
		return tool->Click(ctx, e);
	}
	void Hover(const Vec3 &p) {
		ToolContext ctx = Ctx();
		tool->Hover(ctx, At(Px(p)));
	}
	ToolResult Type(const char *text) {
		ToolContext ctx = Ctx();
		return tool->Type(ctx, ParseMeasurement(String(text)));
	}
	void Use(ToolKind k) { tool = MakeTool(k); }
};

float Volume(const Model &m) {
	const MeshData mesh = BuildMesh(m);
	float v = 0.f;
	for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
		v += mesh.positions[mesh.indices[i]].Dot(
				 mesh.positions[mesh.indices[i + 1]].Cross(mesh.positions[mesh.indices[i + 2]])) /
			 6.f;
	return v;
}

} // namespace

TEST(Inference_SnapsLikeSketchUp) {
	User u;
	u.Use(ToolKind::SELECT);
	(void)PushPull(u.model, u.model.AddFace({{0, 0, 0}, {0, 0, 2}, {2, 0, 2}, {2, 0, 0}}).Unwrap(), 1.f);
	auto infer = [&](const Vec3 &p, Option<Vec3> anchor = NONE, math::FVector2 nudge = {0, 0}) {
		const math::FVector2 px = u.Px(p) + nudge;
		return Infer(u.model, InferenceQuery{u.camera.Ray(px), px, u.camera.Projector_(), anchor});
	};
	CHECK(infer({2, 1, 2}).kind == InferenceKind::ENDPOINT);
	CHECK(infer({2, 1, 1}).kind == InferenceKind::MIDPOINT);
	const Inference onEdge = infer({2, 1, 0.4f});
	CHECK(onEdge.kind == InferenceKind::ON_EDGE);
	CHECK_NEAR(onEdge.point.y, 1.f, 1e-3f);
	const Inference face = infer({1.1f, 1, 0.9f});
	CHECK(face.kind == InferenceKind::ON_FACE);
	CHECK_NEAR(face.planeNormal.y, 1.f, 1e-4f);
	CHECK(infer({6, 0, -3}).kind == InferenceKind::GROUND);
	CHECK(infer({2, 0, 2}).kind == InferenceKind::ENDPOINT);                // coin au sol, visible
	CHECK(infer({0, 0, 0}).kind != InferenceKind::ENDPOINT);                // coin caché par la boîte
	// Axe rouge issu d'un point d'ancrage au sol.
	const Inference axis = infer({5.5f, 0, -1.f}, Some(Vec3{3.5f, 0, -1.f}));
	CHECK(axis.kind == InferenceKind::ON_AXIS_X);
	CHECK_NEAR(axis.point.z, -1.f, 1e-3f);
	CHECK(String(InferenceLabel(axis.kind)) == "Sur l'axe rouge");
}

TEST(Tools_RectangleThenPushPullMakesABox) {
	User u;
	u.Use(ToolKind::RECTANGLE);
	CHECK(!u.Click({0, 0, 0}).changed);
	CHECK(u.tool->IsActive());
	u.Hover({2, 0, 1});
	CHECK(u.tool->Preview(u.Ctx()).measure.Contains("2,000 m × 1,000 m"));
	REQUIRE(u.Click({2, 0, 1}).changed);
	CHECK_EQ(u.model.FaceCount(), 1u);
	CHECK_NEAR(u.model.FaceArea(0), 2.f, 1e-3f);

	u.Use(ToolKind::PUSH_PULL);
	(void)u.Click({1, 0, 0.5f});
	CHECK(u.tool->IsActive());
	REQUIRE(u.Type("1,5").changed); // la boîte de mesures, à la française
	CHECK_NEAR(Volume(u.model), 3.f, 1e-3f);
	CHECK(!u.tool->IsActive());
}

TEST(Tools_RectangleDrawnOnAFaceIsImprintedThenPushedIn) {
	User u;
	(void)PushPull(u.model, u.model.AddFace({{0, 0, 0}, {0, 0, 2}, {2, 0, 2}, {2, 0, 0}}).Unwrap(), 1.f);
	u.Use(ToolKind::RECTANGLE);
	(void)u.Click({0.5f, 1, 0.5f}); // sur le dessus de la boîte
	const ToolResult r = u.Click({1.5f, 1, 1.5f});
	REQUIRE(r.changed);
	CHECK(r.message.Contains("empreinte"));
	u.Use(ToolKind::PUSH_PULL);
	(void)u.Click({1.f, 1, 1.f});
	REQUIRE(u.Type("-0,5").changed); // creuser
	CHECK_NEAR(Volume(u.model), 4.f - 0.5f, 1e-3f);
}

TEST(Tools_LineClosesALoopIntoAFaceAndAcceptsTypedLengths) {
	User u;
	u.Use(ToolKind::LINE);
	(void)u.Click({0, 0, 0});
	u.Hover({3, 0, 0});
	REQUIRE(u.Type("2").changed); // 2 m vers le point survolé
	CHECK(u.model.FindVertex({2, 0, 0}, 1e-3f) != INVALID_ID);
	(void)u.Click({2, 0, 2});
	(void)u.Click({0, 0, 2});
	const ToolResult r = u.Click({0, 0, 0}); // retour au départ
	CHECK(r.message.Contains("Face créée"));
	CHECK_EQ(u.model.FaceCount(), 1u);
	CHECK(!u.tool->IsActive());
	// Une ligne tracée d'un bord à l'autre coupe la face.
	(void)u.Click({1, 0, 0});
	CHECK(u.Click({1, 0, 2}).message.Contains("coupée"));
	CHECK_EQ(u.model.FaceCount(), 2u);
}

TEST(Tools_CircleOffsetMoveRotateScale) {
	User u;
	u.Use(ToolKind::CIRCLE);
	(void)u.Click({1, 0, 1});
	u.Hover({2, 0, 1});
	REQUIRE(u.Type("0,5").changed);
	const FaceId disk = 0;
	CHECK_NEAR(u.model.FaceArea(disk), math::PI * 0.25f, 0.02f);

	u.Use(ToolKind::OFFSET);
	(void)u.Click({1.f, 0, 1.4f});
	REQUIRE(u.Type("0,1").changed);
	CHECK_EQ(u.model.FaceCount(), 2u);

	u.selection.faces = {disk};
	u.Use(ToolKind::MOVE);
	(void)u.Click({1, 0, 1});
	u.Hover({3, 0, 1});
	REQUIRE(u.Type("1").changed);
	CHECK_NEAR(u.model.FaceCentroid(disk).x, 2.f, 1e-3f);

	u.Use(ToolKind::SCALE);
	(void)u.Click(u.model.OuterPoints(disk)[0]);
	const float before = u.model.FaceArea(disk);
	REQUIRE(u.Type("2").changed);
	CHECK_NEAR(u.model.FaceArea(disk), before * 4.f, 1e-2f);

	u.Use(ToolKind::ROTATE);
	(void)u.Click({2, 0, 1});
	(void)u.Click({3, 0, 1});
	REQUIRE(u.Type("90").changed);
	CHECK_NEAR(u.model.FaceArea(disk), before * 4.f, 1e-2f);
}

TEST(Tools_MoveWithCtrlCopies) {
	User u;
	u.model.AddFace({{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {1, 0, 0}});
	u.selection.faces = {0};
	u.Use(ToolKind::MOVE);
	(void)u.Click({0, 0, 0});
	u.Hover({4, 0, 0});
	const ToolResult r = u.Click({3, 0, 0}, true);
	CHECK(r.message.Contains("Copie"));
	CHECK_EQ(u.model.FaceCount(), 2u);
}

TEST(Tools_FollowMeEraserPaintTape) {
	User u;
	u.material = u.model.AddMaterial("Bleu", {0, 0, 1});
	// Profil vertical à l'origine, chemin le long de x puis z, sélectionné.
	(void)u.model.AddFace({{0, 0, 0}, {0, 0.3f, 0}, {0, 0.3f, 0.3f}, {0, 0, 0.3f}});
	const auto path = u.model.AddPolyline({{0, 0.15f, 0.15f}, {3, 0.15f, 0.15f}, {3, 0.15f, 3}});
	u.selection.edges = path;
	u.Use(ToolKind::FOLLOW_ME);
	REQUIRE(u.Click({0, 0.2f, 0.1f}).changed);
	CHECK(u.model.FaceCount() > 5u);

	u.Use(ToolKind::PAINT);
	const Inference topFace = u.At(u.Px({1.5f, 0.3f, 0.15f})).at;
	REQUIRE(topFace.face != INVALID_ID);
	REQUIRE(u.Click({1.5f, 0.3f, 0.15f}).changed);
	CHECK_EQ(u.model.Faces()[topFace.face].material, u.material);

	u.Use(ToolKind::TAPE);
	(void)u.Click({0, 0, -1.5f}); // zone dégagée : pas d'accroche parasite
	CHECK(u.Click({3, 0, -1.5f}).message.Contains("3,000 m"));

	u.Use(ToolKind::ERASER);
	const size_t faces = u.model.FaceCount();
	REQUIRE(u.Click({1.5f, 0.3f, 0.15f}).changed);
	CHECK(u.model.FaceCount() < faces);
}

TEST(Tools_ShortcutsAndMeasurementParsing) {
	CHECK(ToolFromShortcut('p') == Some(ToolKind::PUSH_PULL));
	CHECK(ToolFromShortcut('z').IsNone());
	CHECK(ParseMeasurement(String("2,5")) == std::vector<float>{2.5f});
	const std::vector<float> two = ParseMeasurement(String("120cm;3m"));
	REQUIRE_EQ(two.size(), 2u);
	CHECK_NEAR(two[0], 1.2f, 1e-6f);
	CHECK_NEAR(two[1], 3.f, 1e-6f);
	CHECK(ParseMeasurement(String("45°")) == std::vector<float>{45.f});
	CHECK(ParseMeasurement(String("abc")).empty());
	CHECK(FormatLength(1.25f) == "1,250 m");
}

TEST(Imprint_ShapeTouchingTheBorderCutsTheHost) {
	// Une porte posée sur le bas d'un mur : le mur est découpé en U, la porte
	// devient une face à part, poussable seule.
	Model m;
	const FaceId wall = m.AddFace({{0, 0, 0}, {4, 0, 0}, {4, 2.5f, 0}, {0, 2.5f, 0}}).Unwrap();
	const FaceId door = m.AddFace({{1.6f, 0, 0}, {2.4f, 0, 0}, {2.4f, 1.8f, 0}, {1.6f, 1.8f, 0}}).Unwrap();
	CHECK(m.Imprint(door).IsSome());
	size_t alive = 0;
	float area = 0.f;
	for (size_t f = 0; f < m.Faces().size(); ++f)
		if (m.Faces()[f].alive) {
			++alive;
			area += m.FaceArea(FaceId(f));
		}
	CHECK_EQ(alive, size_t(2));
	CHECK_NEAR(area, 10.f, 1e-3f); // aucune surface en double
	CHECK(m.IsAlive(door));
	CHECK_NEAR(m.FaceArea(door), 0.8f * 1.8f, 1e-4f);
	(void)wall;
	// Rectangle traversant toute la hauteur : deux morceaux de mur + la bande.
	Model band;
	(void)band.AddFace({{0, 0, 0}, {4, 0, 0}, {4, 2, 0}, {0, 2, 0}});
	const FaceId strip = band.AddFace({{1, 0, 0}, {2, 0, 0}, {2, 2, 0}, {1, 2, 0}}).Unwrap();
	CHECK(band.Imprint(strip).IsSome());
	alive = 0;
	area = 0.f;
	for (size_t f = 0; f < band.Faces().size(); ++f)
		if (band.Faces()[f].alive) {
			++alive;
			area += band.FaceArea(FaceId(f));
		}
	CHECK_EQ(alive, size_t(3));
	CHECK_NEAR(area, 8.f, 1e-3f);
}

TEST(Inference_WorkPlaneKeepsSketchesOnThePlane) {
	// Esquisse sur le plan vertical z = 0 (rouge/vert), vue de face.
	TestCamera camera{{1.f, 2.5f, 8.f}, {1.f, 1.f, 0.f}};
	Model model;
	(void)PushPull(model, model.AddFace({{3, 0, -1}, {4, 0, -1}, {4, 0, 1}, {3, 0, 1}}).Unwrap(), 1.f);
	InferenceSettings settings;
	settings.workPlane = Some(InferenceSettings::WorkPlane{{0, 0, 0}, {0, 0, 1}});
	auto infer = [&](const Vec3 &p) {
		const math::FVector2 px = camera.Project(p).Unwrap();
		return Infer(model, InferenceQuery{camera.Ray(px), px, camera.Projector_()}, settings);
	};
	const Inference free = infer({0.5f, 1.5f, 0.f});
	CHECK(free.kind == InferenceKind::ON_PLANE);
	CHECK_NEAR(free.point.z, 0.f, 1e-4f);
	CHECK_NEAR(free.planeNormal.z, 1.f, 1e-4f);
	// Le coin de la boîte en z = 1 est HORS du plan : pas d'accroche dessus.
	CHECK(infer({4.f, 0.f, 1.f}).kind != InferenceKind::ENDPOINT);
	CHECK_NEAR(infer({4.f, 0.f, 1.f}).point.z, 0.f, 1e-4f);
	// Le milieu de l'arête de base (3, 0, -1)-(3, 0, 1) est sur le plan : accroché.
	CHECK(infer({3.f, 0.f, 0.f}).kind == InferenceKind::MIDPOINT);

	// Un rectangle esquissé sur ce plan est vertical.
	Selection selection;
	ToolContext ctx{model, selection};
	std::unique_ptr<Tool> rect = MakeTool(ToolKind::RECTANGLE);
	const size_t before = model.Faces().size();
	rect->Hover(ctx, ToolEvent{infer({0.f, 0.5f, 0.f}), {}});
	(void)rect->Click(ctx, ToolEvent{infer({0.f, 0.5f, 0.f}), {}});
	rect->Hover(ctx, ToolEvent{infer({1.5f, 2.f, 0.f}), {}});
	const ToolResult done = rect->Click(ctx, ToolEvent{infer({1.5f, 2.f, 0.f}), {}});
	CHECK(!done.error.IsSome());
	CHECK_EQ(model.Faces().size(), before + 1);
	CHECK_NEAR(std::fabs(model.Faces().back().normal.z), 1.f, 1e-4f);
}
