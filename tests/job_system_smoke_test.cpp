// Tests unitaires — jobs::JobSystem (vivier de fils + ParallelFor).
//
// Vérifie ce qui compte vraiment pour un ordonnanceur : le résultat est
// IDENTIQUE avec et sans parallélisme, chaque indice est visité exactement une
// fois, l'appel est bien bloquant, et le vivier se démonte proprement.
#define USE_TEST

#include "core/test.hpp"
#include "jobs/job_system.hpp"

#include <atomic>

TEST(JobSystem, InlineModeRunsEverythingOnTheCaller) {
	jobs::JobSystem jobs(0);
	EXPECT_TRUE(jobs.IsInline());
	EXPECT_EQ(int(jobs.WorkerCount()), 0);

	std::vector<int> visited(100, 0);
	jobs.ParallelFor(visited.size(), 1, [&visited](size_t i) { visited[i] = 1; });
	for (int v : visited)
		EXPECT_EQ(v, 1);
}

TEST(JobSystem, VisitsEveryIndexExactlyOnce) {
	jobs::JobSystem jobs(4);
	constexpr size_t COUNT = 10000;
	std::vector<std::atomic<int>> visits(COUNT);
	for (auto &v : visits)
		v.store(0);

	jobs.ParallelFor(COUNT, 16, [&visits](size_t i) { visits[i].fetch_add(1); });

	for (size_t i = 0; i < COUNT; ++i)
		if (visits[i].load() != 1) {
			test::ReportFailure(__FILE__, __LINE__, "un indice n'a pas été visité exactement une fois");
			return;
		}
	EXPECT_TRUE(true);
}

TEST(JobSystem, ParallelResultMatchesSerialResult) {
	// Le test qui compte : même entrée, même sortie, avec ou sans fils.
	constexpr size_t COUNT = 5000;
	auto compute = [](size_t i) { return double(i) * 0.5 + double(i % 7); };

	std::vector<double> serial(COUNT, 0.0);
	jobs::JobSystem inlineJobs(0);
	inlineJobs.ParallelFor(COUNT, 1, [&](size_t i) { serial[i] = compute(i); });

	std::vector<double> parallel(COUNT, 0.0);
	jobs::JobSystem threadedJobs(4);
	threadedJobs.ParallelFor(COUNT, 8, [&](size_t i) { parallel[i] = compute(i); });

	for (size_t i = 0; i < COUNT; ++i)
		if (serial[i] != parallel[i]) {
			test::ReportFailure(__FILE__, __LINE__, "résultat parallèle différent du résultat série");
			return;
		}
	EXPECT_TRUE(true);
}

TEST(JobSystem, ParallelForIsBlocking) {
	// Au retour, TOUT est fini : c'est ce qui autorise `ParallelFor` à
	// capturer son corps par référence.
	jobs::JobSystem jobs(3);
	std::atomic<int> done{0};
	jobs.ParallelFor(500, 4, [&done](size_t) { done.fetch_add(1); });
	EXPECT_EQ(done.load(), 500);
}

TEST(JobSystem, SmallWorkloadsStayInline) {
	// En dessous de `minimumPerBatch * 2`, rien ne part au vivier : la
	// synchronisation coûterait plus cher que le travail.
	jobs::JobSystem jobs(4);
	std::vector<int> values(5, 0);
	jobs.ParallelFor(values.size(), 100, [&values](size_t i) { values[i] = int(i); });
	for (size_t i = 0; i < values.size(); ++i)
		EXPECT_EQ(values[i], int(i));
	EXPECT_EQ(jobs.PeakConcurrentWorkers(), 0); // aucune tâche n'a été déposée
}

TEST(JobSystem, ReportsConcurrency) {
	jobs::JobSystem jobs(4);
	EXPECT_TRUE(jobs.WorkerCount() == 4);
	EXPECT_FALSE(jobs.IsInline());
	// Une charge assez grosse pour que plusieurs lots partent réellement.
	std::vector<double> values(200000, 0.0);
	jobs.ParallelFor(values.size(), 64, [&values](size_t i) { values[i] = double(i) * 1.5; });
	EXPECT_TRUE(jobs.PeakConcurrentWorkers() >= 1);
	EXPECT_TRUE(jobs.PeakConcurrentWorkers() <= 4);
	EXPECT_EQ(values[199999], 199999.0 * 1.5);
}

TEST(JobSystem, ZeroCountAndRepeatedUseAreSafe) {
	jobs::JobSystem jobs(2);
	jobs.ParallelFor(0, 1, [](size_t) { EXPECT_TRUE(false); }); // ne doit rien appeler
	int total = 0;
	for (int round = 0; round < 20; ++round) {
		std::vector<int> values(1000, 1);
		jobs.ParallelFor(values.size(), 8, [&values](size_t i) { values[i] = int(i) % 3; });
		for (int v : values)
			total += v;
	}
	EXPECT_TRUE(total > 0);
}

TEST(JobSystem, RecommendedWorkerCountLeavesOneCoreToTheCaller) {
	size_t recommended = jobs::JobSystem::RecommendedWorkerCount();
	EXPECT_TRUE(recommended + 1 <= size_t(sdl3::system::CpuCount() > 0 ? sdl3::system::CpuCount() : 1) || recommended == 0);
}

int main() { return RUN_ALL_TESTS(); }
