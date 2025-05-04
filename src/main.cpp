#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <semaphore>
#include <atomic>
#include <chrono>
#include <random>

using namespace std;

// Global variables for synchronization
constexpr int NUM_JOGADORES = 4;
counting_semaphore<NUM_JOGADORES> cadeira_sem(NUM_JOGADORES - 1); // Inicia com n-1 cadeiras, capacidade máxima n
condition_variable music_cv;
mutex music_mutex;
atomic<bool> musica_parada{false};
atomic<bool> jogo_ativo{true};

/*
 * Uso básico de um counting_semaphore em C++:
 * 
 * O `std::counting_semaphore` é um mecanismo de sincronização que permite controlar o acesso a um recurso compartilhado 
 * com um número máximo de acessos simultâneos. Neste projeto, ele é usado para gerenciar o número de cadeiras disponíveis.
 * Inicializamos o semáforo com `n - 1` para representar as cadeiras disponíveis no início do jogo. 
 * Cada jogador que tenta se sentar precisa fazer um `acquire()`, e o semáforo permite que até `n - 1` jogadores 
 * ocupem as cadeiras. Quando todos os assentos estão ocupados, jogadores adicionais ficam bloqueados até que 
 * o coordenador libere o semáforo com `release()`, sinalizando a eliminação dos jogadores.
 * O método `release()` também pode ser usado para liberar múltiplas permissões de uma só vez, por exemplo: `cadeira_sem.release(3);`,
 * o que permite destravar várias threads de uma só vez, como é feito na função `liberar_threads_eliminadas()`.
 *
 * Métodos da classe `std::counting_semaphore`:
 * 
 * 1. `acquire()`: Decrementa o contador do semáforo. Bloqueia a thread se o valor for zero.
 *    - Exemplo de uso: `cadeira_sem.acquire();` // Jogador tenta ocupar uma cadeira.
 * 
 * 2. `release(int n = 1)`: Incrementa o contador do semáforo em `n`. Pode liberar múltiplas permissões.
 *    - Exemplo de uso: `cadeira_sem.release(2);` // Libera 2 permissões simultaneamente.
 */

// Classes
class JogoDasCadeiras {
public:
    JogoDasCadeiras(int num_jogadores)
        : num_jogadores(num_jogadores), cadeiras(num_jogadores - 1),
         jogadores_restantes(num_jogadores) {}

    void iniciar_rodada() {
        std::lock_guard<std::mutex> lock(jogo_mutex);
        
        if (cadeiras > 1) {
            cadeiras--;
        }

        int esgotar = 0;
        while (cadeira_sem.try_acquire()) {
            ++esgotar;
        }

        cadeira_sem.release(cadeiras);
        musica_parada.store(false);

        cout << "Iniciando rodada com " << jogadores_restantes << " jogadores e " << (NUM_JOGADORES-1) << " cadeiras." << endl; 
    }

    void parar_musica() {
        {
            lock_guard<mutex> lock(music_mutex);
            musica_parada = true;
        }
        music_cv.notify_all();
        cout << "🔇 Música parou! Corram para as cadeiras!\n";
    }

    int getJogadoresRestantes() {
        std::lock_guard<std::mutex> lock(jogo_mutex);
        return jogadores_restantes;
    }

    void eliminar_jogador(int jogador_id) {
        std::lock_guard<std::mutex> lock(jogo_mutex);
        jogadores_restantes--;
        cout << "❌ Jogador " << jogador_id << " foi eliminado!\n";
    }

    void exibir_estado() {
        std::lock_guard<std::mutex> lock(jogo_mutex);
        cout << "📊 Jogadores restantes: " << jogadores_restantes
             << ", Cadeiras disponíveis: " << cadeiras << endl;
    }

private:
    int num_jogadores;
    int cadeiras;
    int jogadores_restantes;
    mutex jogo_mutex; 
};

class Jogador {
public:
    Jogador(int id, JogoDasCadeiras& jogo)
        : id(id), jogo(jogo) {}

    void tentar_ocupar_cadeira() {
        if (cadeira_sem.try_acquire()) {
            cout << "✅ Jogador " << id << " conseguiu uma cadeira.\n";
        } else {
            eliminado = true;
        }
    }

    void verificar_eliminacao() {
        if (eliminado) {
            jogo.eliminar_jogador(id);
            jogo_ativo = false;
        }
    }

    void joga() {
        while (jogo_ativo.load()) {
            unique_lock<mutex> lock(music_mutex);
            music_cv.wait(lock, [] { return musica_parada.load() || !jogo_ativo.load(); });
            lock.unlock();

            if (!jogo_ativo.load()) break;

            if (eliminado) return;  // jogador já foi eliminado

            tentar_ocupar_cadeira();
            verificar_eliminacao();

            if (eliminado) return; // encerra a thread
        }
    }

private:
    int id;
    bool eliminado = false;
    JogoDasCadeiras& jogo;
};

class Coordenador {
public:
    Coordenador(JogoDasCadeiras& jogo)
        : jogo(jogo) {}

    void iniciar_jogo() {
        while (jogo.getJogadoresRestantes() > 1) {
            jogo.iniciar_rodada();

            random_device rd;
            mt19937 gen(rd());
            uniform_int_distribution<> dist(1000, 3000);
            std::this_thread::sleep_for(std::chrono::milliseconds(dist(gen)));

            // Para a música e deixa os jogadores tentarem se sentar
            jogo.parar_musica();

            std::this_thread::sleep_for(chrono::milliseconds(1000)); // Tempo para jogadores tentarem sentar

            liberar_threads_eliminadas(); // Libera qualquer thread travada para não dar deadlock
        }

        cout << "\n🏆 Jogador restante venceu o Jogo das Cadeiras!\n";
        jogo_ativo = false; // Termina o jogo
    }

    void liberar_threads_eliminadas() {
        // Libera múltiplas permissões no semáforo para destravar todas as threads que não conseguiram se sentar
        cadeira_sem.release(NUM_JOGADORES - 1); // Libera o número de permissões igual ao número de jogadores que ficaram esperando
    }

private:
    JogoDasCadeiras& jogo;
};

// Main function
int main() {
    JogoDasCadeiras jogo(NUM_JOGADORES);
    Coordenador coordenador(jogo);
    std::vector<std::thread> jogadores;

    // Criação das threads dos jogadores
    std::vector<Jogador> jogadores_objs;
    for (int i = 1; i <= NUM_JOGADORES; ++i) {
        jogadores_objs.emplace_back(i, jogo);
    }

    for (int i = 0; i < NUM_JOGADORES; ++i) {
        jogadores.emplace_back(&Jogador::joga, &jogadores_objs[i]);
    }

    // Thread do coordenador
    std::thread coordenador_thread(&Coordenador::iniciar_jogo, &coordenador);

    // Esperar pelas threads dos jogadores
    for (auto& t : jogadores) {
        if (t.joinable()) {
            t.join();
        }
    }

    // Esperar pela thread do coordenador
    if (coordenador_thread.joinable()) {
        coordenador_thread.join();
    }

    std::cout << "Jogo das Cadeiras finalizado." << std::endl;
    return 0;
}

