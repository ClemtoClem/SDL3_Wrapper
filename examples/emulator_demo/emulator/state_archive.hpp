/**
 * @file state_archive.h
 * @brief Archive binaire unique utilisée à la fois pour la sauvegarde et le
 *        chargement d'un état de l'émulateur (save state).
 *
 * L'idée est d'avoir un seul chemin de code par composant : chaque classe
 * expose une méthode `ioState(StateArchive&)` qui lit ou écrit ses champs
 * dans le même ordre, selon le mode de l'archive (`saving`). Cela évite toute
 * divergence entre la sauvegarde et le chargement (ordre des champs identique
 * des deux côtés).
 */

#ifndef STATE_ARCHIVE_H
#define STATE_ARCHIVE_H

#include <cstdint>
#include <cstring>
#include <queue>
#include <type_traits>
#include <vector>

namespace emulator_demo {

class StateArchive {
  public:
    bool saving; ///< true = écriture (save), false = lecture (load).

    explicit StateArchive(bool saving) : saving(saving) {
    }

    /**
     * @brief Construit une archive de chargement à partir de données déjà lues.
     * @param data Contenu binaire précédemment produit par une archive de sauvegarde.
     */
    explicit StateArchive(std::vector<uint8_t> data) : saving(false), buffer(std::move(data)) {
    }

    /**
     * @brief Lit ou écrit un bloc d'octets bruts.
     * @param data Pointeur vers les données à écrire (save) ou à remplir (load).
     * @param size Taille en octets du bloc.
     */
    void io(void *data, size_t size) {
        if (saving) {
            size_t off = buffer.size();
            buffer.resize(off + size);
            std::memcpy(buffer.data() + off, data, size);
        } else {
            // A corrupt file, a version mismatch, or a single ioState()
            // asymmetry anywhere in the component chain would otherwise
            // desync the read cursor and pull garbage bytes (e.g. read as a
            // string/array length elsewhere) past the end of `buffer` —
            // reject it here instead of reading out of bounds.
            //
            // Pas d'exception (règle du dépôt) : l'archive passe en échec et
            // rend des zéros pour toutes les lectures suivantes, ce qui garde
            // les boucles pilotées par des longueurs lues (files, noms) à
            // zéro itération. L'appelant teste `failed` après ioState().
            if (failed || (size > 0 && (readPos > buffer.size() || size > buffer.size() - readPos))) {
                failed = true;
                std::memset(data, 0, size);
                return;
            }
            std::memcpy(data, buffer.data() + readPos, size);
            readPos += size;
        }
    }

    /**
     * @brief Lit ou écrit un objet trivialement copiable (registre, structure POD...).
     */
    template <typename T> void io(T &v) {
        static_assert(std::is_trivially_copyable_v<T>, "StateArchive::io() requires a trivially copyable type");
        io(&v, sizeof(T));
    }

    /**
     * @brief Lit ou écrit un tableau statique d'objets trivialement copiables.
     */
    template <typename T, size_t N> void io(T (&v)[N]) {
        static_assert(std::is_trivially_copyable_v<T>, "StateArchive::io() requires a trivially copyable type");
        io(&v[0], sizeof(v));
    }

    /**
     * @brief Lit ou écrit une std::queue de type trivialement copiable (FIFO IPC, FIFO GX...).
     */
    template <typename T> void ioQueue(std::queue<T> &q) {
        if (saving) {
            uint32_t n = uint32_t(q.size());
            io(n);
            std::queue<T> copy = q;
            while (!copy.empty()) {
                T v = copy.front();
                io(v);
                copy.pop();
            }
        } else {
            uint32_t n = 0;
            io(n);
            q = {};
            for (uint32_t i = 0; i < n; i++) {
                T v{};
                io(v);
                q.push(v);
            }
        }
    }

    std::vector<uint8_t> buffer; ///< Rempli pendant une sauvegarde, consommé pendant un chargement.
    size_t                readPos = 0;
    bool                  failed  = false; ///< Lecture au-delà de la fin du tampon (état corrompu/incompatible).
};

} // namespace emulator_demo

#endif /* STATE_ARCHIVE_H */
