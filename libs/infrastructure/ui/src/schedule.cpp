// Systèmes ui pour ecs::Schedule
#include <ui/systems.hpp>

namespace ui {

void UiInputSystem(ecs::SystemContext &sc) {
	Context *ctx = ContextOf(sc.world);
	if (!ctx)
		return;
	std::vector<sdl3::Event> events;
	events.swap(ctx->queued);
	for (const sdl3::Event &ev : events)
		(void)InputSystem::HandleEvent(sc.world, *ctx, ev);
}

void UiAnimationSystem(ecs::SystemContext &sc) {
	if (Context *ctx = ContextOf(sc.world))
		AnimationSystem::Run(sc.world, *ctx, sc.dt);
}

void UiStyleSystem(ecs::SystemContext &sc) {
	if (Context *ctx = ContextOf(sc.world))
		StyleSystem::Run(sc.world, *ctx);
}

void UiLayoutSystem(ecs::SystemContext &sc) {
	Context *ctx = ContextOf(sc.world);
	if (!ctx)
		return;
	if (ctx->window) {
		const sdl3::Point size = ctx->window->GetSize();
		if (float(size.x) != ctx->viewport.x || float(size.y) != ctx->viewport.y) {
			ctx->viewport = {float(size.x), float(size.y)};
			ctx->MarkLayoutDirty();
		}
	}
	(void)LayoutSystem::Run(sc.world, *ctx);
}

void UiRenderSystem(ecs::SystemContext &sc) {
	Context *ctx = ContextOf(sc.world);
	if (!ctx)
		return;
	for (Extension &ext : ctx->extensions)
		if (ext.prepare)
			ext.prepare(sc.world, *ctx);
	RenderSystem::Run(sc.world, *ctx);
}

void Install(ecs::Schedule &schedule, bool withRender) {
	schedule.Add(ecs::Stage::PRE_UPDATE, "ui_input", UiInputSystem, -100);
	schedule.Add(ecs::Stage::UPDATE, "ui_animation", UiAnimationSystem, 100);
	schedule.Add(ecs::Stage::POST_UPDATE, "ui_style", UiStyleSystem, 100);
	schedule.Add(ecs::Stage::POST_UPDATE, "ui_layout", UiLayoutSystem, 101);
	if (withRender)
		schedule.Add(ecs::Stage::POST_UPDATE, "ui_render", UiRenderSystem, 1000);
}

void PushEvent(ecs::ArchetypeRegistry &world, const sdl3::Event &ev) {
	if (Context *ctx = ContextOf(world))
		ctx->queued.push_back(ev);
}

} // namespace ui
