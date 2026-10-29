#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>

using namespace geode::prelude;

bool g_clickRetenido = false;
int g_framesEsperados = 0;
bool g_enviandoClickCorregido = false;

bool saltoEsMortal(PlayLayer* pl, PlayerObject* player) {
    if (!pl || !player) return false;

    CCPoint posActual = player->getPosition();
    float gravedadDir = player->m_isUpsideDown ? -1.0f : 1.0f;

    const float pasosX[] = { 18.0f, 38.0f, 60.0f };
    const float pasosY[] = { 15.0f, 32.0f, 44.0f };

    CCArray* objetos = pl->m_objects;
    if (!objetos) return false;

    for (int p = 0; p < 3; ++p) {
        float futX = posActual.x + pasosX[p];
        float futY = posActual.y + (pasosY[p] * gravedadDir);
        
        CCRect hitboxFutura(futX - 12.0f, futY - 12.0f, 24.0f, 24.0f);

        for (unsigned int i = 0; i < objetos->count(); ++i) {
            auto obj = static_cast<GameObject*>(objetos->objectAtIndex(i));
            if (!obj) continue;

            if (std::abs(obj->getPositionX() - futX) > 90.0f) continue;

            if (obj->m_objectType == GameObjectType::Hazard || 
                obj->m_objectType == GameObjectType::AnimatedHazard) {
                
                if (hitboxFutura.intersectsRect(obj->getObjectRect())) {
                    return true;
                }
            }
        }
    }

    return false;
}

class $modify(CorrectorInput, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (g_enviandoClickCorregido) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        bool modActivo = Mod::get()->getSettingValue<bool>("mod-activo");
        int maxFramesAyuda = static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-frames"));

        auto pl = PlayLayer::get();

        if (modActivo && maxFramesAyuda > 0 && pl && down && isPlayer1 && button == 1) {
            if (m_player1 && !m_player1->m_isDead && saltoEsMortal(pl, m_player1)) {
                g_clickRetenido = true;
                g_framesEsperados = 0;
                return;
            }
        }

        if (!down && isPlayer1 && button == 1) {
            g_clickRetenido = false;
            g_framesEsperados = 0;
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(CorrectorLoop, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!g_clickRetenido || !m_player1 || m_player1->m_isDead) {
            return;
        }

        int maxFramesAyuda = static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-frames"));
        g_framesEsperados++;

        bool yaEsSeguro = !saltoEsMortal(this, m_player1);
        bool limiteAlcanzado = (g_framesEsperados >= maxFramesAyuda);

        if (yaEsSeguro || limiteAlcanzado) {
            g_clickRetenido = false;
            g_enviandoClickCorregido = true;

            this->handleButton(true, 1, true);

            g_enviandoClickCorregido = false;
            g_framesEsperados = 0;
        }
    }

    void resetLevel() {
        g_clickRetenido = false;
        g_framesEsperados = 0;
        g_enviandoClickCorregido = false;
        PlayLayer::resetLevel();
    }
};

class $modify(MenuPausaCorrector, PauseLayer) {
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
            menu_selector(MenuPausaCorrector::onAbrirConfiguracion)
        );
        botonConfig->setID("boton-click-corrector"_spr);

        leftMenu->addChild(botonConfig);
        leftMenu->updateLayout();
    }

    void onAbrirConfiguracion(CCObject* sender) {
        geode::openSettingsPopup(Mod::get());
    }
};
