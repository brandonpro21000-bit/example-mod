#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>

using namespace geode::prelude;

// Estado del Predictive Buffer
bool g_bufferActivo = false;
bool g_dedoSoltado = false;
int g_framesRestantes = 0;
bool g_ejecutandoSalto = false;

// Obtiene la velocidad horizontal en píxeles por frame (a 60 FPS base)
float obtenerVelocidadX(PlayerObject* p) {
    float mult = p->m_playerSpeed;
    // Valores aproximados del motor de GD según el portal de velocidad
    if (mult <= 0.7f) return 4.18f;  // Lento (0.5x)
    if (mult <= 0.9f) return 5.19f;  // Normal (1x)
    if (mult <= 1.1f) return 6.45f;  // Rápido (2x)
    if (mult <= 1.3f) return 7.80f;  // Muy rápido (3x)
    return 9.60f;                    // Cuádruple (4x)
}

// Revisa si un punto (x, y) del jugador choca con algún peligro mortal
bool puntoChocaConPeligro(PlayLayer* pl, float x, float y, float radioHitbox) {
    if (!pl || !pl->m_objects) return false;

    CCRect rectJugador(x - radioHitbox, y - radioHitbox, radioHitbox * 2.0f, radioHitbox * 2.0f);

    for (auto* obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (!obj) continue;

        float dx = std::abs(obj->getPositionX() - x);
        if (dx > 60.0f) continue;

        if (obj->m_objectType == GameObjectType::Hazard || 
            obj->m_objectType == GameObjectType::AnimatedHazard) {
            
            CCRect r = obj->getObjectRect();
            // Ajuste fino de hitbox interna de pinchos en GD
            r.origin.x += 5.0f;
            r.origin.y += 5.0f;
            r.size.width = std::max(3.0f, r.size.width - 10.0f);
            r.size.height = std::max(3.0f, r.size.height - 10.0f);

            if (rectJugador.intersectsRect(r)) {
                return true;
            }
        }
    }
    return false;
}

// Simula la parábola completa del salto desde la posición actual
bool simularSaltoEsMortal(PlayLayer* pl, PlayerObject* p) {
    if (!pl || !p) return false;

    CCPoint pos = p->getPosition();
    float dirGrav = p->m_isUpsideDown ? -1.0f : 1.0f;
    float escala = p->m_vehicleSize; // 1.0 normal, ~0.6 mini
    float radio = 10.0f * escala;

    float vx = obtenerVelocidadX(p);
    // Impulso inicial de salto y gravedad por frame (60Hz equiv)
    float vy = (10.8f * escala) * dirGrav;
    float gravedad = (-0.86f * escala) * dirGrav;

    float simX = pos.x;
    float simY = pos.y;
    float sueloY = pos.y;

    // Simulamos los próximos 30 frames del arco de salto
    for (int step = 1; step <= 30; ++step) {
        simX += vx;
        simY += vy;
        vy += gravedad;

        // No bajar más allá del nivel del suelo desde el que saltó
        if (dirGrav > 0.0f && simY < sueloY) simY = sueloY;
        if (dirGrav < 0.0f && simY > sueloY) simY = sueloY;

        if (puntoChocaConPeligro(pl, simX, simY, radio)) {
            return true; // En algún punto de este arco o al aterrizar toca un pincho
        }
    }

    return false; // El salto limpia los obstáculos
}

// Decide si debemos retener el clic en este frame y esperar al siguiente
bool debeRetenerClick(PlayLayer* pl, PlayerObject* p) {
    if (!pl || !p || p->m_isDead) return false;

    // Solo aplicamos predicción de salto en modos de toque discreto (Cubo, Bola, UFO, Araña, Robot)
    // En Nave o Wave el control es continuo al mantener presionado
    if (p->m_isShip || p->m_isDart || p->m_isSwing) {
        return false;
    }

    // CASO 1: El jugador está en el aire cayendo (aún no toca el suelo).
    // Si da clic unos frames antes de aterrizar, retenemos el clic en el buffer
    // para que salte automáticamente en el instante en que toque el piso.
    if (!p->m_isOnGround) {
        return true;
    }

    // CASO 2: El jugador ya está en el suelo.
    // Primero verificamos si esperar 1 frame más en el suelo nos estrellaría de inmediato.
    CCPoint pos = p->getPosition();
    float vx = obtenerVelocidadX(p);
    float radio = 11.0f * p->m_vehicleSize;

    // Si justo delante de nosotros (en los próximos 2 frames en el piso) ya hay un pincho,
    // NO debemos retrasar el salto: tiene que saltar AHORA MISMO.
    if (puntoChocaConPeligro(pl, pos.x + vx * 1.5f, pos.y, radio)) {
        return false;
    }

    // Si el suelo inmediato está libre, simulamos si saltar ahora mismo nos haría caer sobre un pincho más adelante
    if (simularSaltoEsMortal(pl, p)) {
        return true; // Saltar ahora nos mata al caer; esperamos 1 frame más
    }

    return false; // Es seguro saltar en este frame
}

// 1. Interceptar las pulsaciones del jugador
class $modify(PredictiveInput, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (g_ejecutandoSalto) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        bool modActivo = Mod::get()->getSettingValue<bool>("mod-activo");
        int maxFrames = static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-frames"));
        auto pl = PlayLayer::get();

        if (modActivo && maxFrames > 0 && pl && isPlayer1 && button == 1 && m_player1 && !m_player1->m_isDead) {
            if (down) {
                if (debeRetenerClick(pl, m_player1)) {
                    g_bufferActivo = true;
                    g_dedoSoltado = false;
                    g_framesRestantes = maxFrames;
                    return; // Guardamos el clic en el Predictive Buffer
                }
            } else {
                // Si levantó el dedo mientras el clic estaba en el buffer,
                // recordamos que ya soltó pero NO cancelamos el salto pendiente
                if (g_bufferActivo) {
                    g_dedoSoltado = true;
                    return;
                }
            }
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

// 2. Evaluar el buffer frame por frame
class $modify(PredictiveLoop, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!g_bufferActivo || !m_player1 || m_player1->m_isDead) {
            return;
        }

        g_framesRestantes--;

        bool ahoraEsSeguroSaltar = !debeRetenerClick(this, m_player1);
        bool tiempoAgotado = (g_framesRestantes <= 0);

        // Si ya tocó el suelo y la parábola es segura (o se acabaron los frames de ayuda)
        if (ahoraEsSeguroSaltar || tiempoAgotado) {
            g_bufferActivo = false;
            g_ejecutandoSalto = true;

            // Ejecutamos la pulsación
            this->handleButton(true, 1, true);

            // Si el jugador solo dio un toque rápido y ya había quitado el dedo,
            // soltamos el botón solo si ya logró despegar del suelo (o si se agotó el tiempo)
            if (g_dedoSoltado) {
                this->handleButton(false, 1, true);
                g_dedoSoltado = false;
            }

            g_ejecutandoSalto = false;
        }
    }

    void resetLevel() {
        g_bufferActivo = false;
        g_dedoSoltado = false;
        g_framesRestantes = 0;
        g_ejecutandoSalto = false;
        PlayLayer::resetLevel();
    }
};

// 3. Botón de configuración en el menú de pausa
class $modify(MenuPausaPredictive, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto leftMenu = this->getChildByID("left-button-menu");
        if (!leftMenu) return;

        auto spriteBoton = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
        if (!spriteBoton) return;
        spriteBoton->setScale(0.8f);

        auto botonConfig = CCMenuItemSpriteExtra::create(
            spriteBoton,
            this,
            menu_selector(MenuPausaPredictive::onAbrirConfiguracion)
        );
        botonConfig->setID("boton-click-corrector"_spr);

        leftMenu->addChild(botonConfig);
        leftMenu->updateLayout();
    }

    void onAbrirConfiguracion(CCObject* sender) {
        geode::openSettingsPopup(Mod::get());
    }
};
