#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>

#include "GeneratorPopup.hpp"
#include "Trace.hpp"

using namespace geode::prelude;

class $modify(RGEditorUI, EditorUI) {
    struct Fields {
        CCMenu* menu = nullptr;
    };

    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        auto win = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setID("generator-menu"_spr);
        menu->setPosition({win.width / 2, win.height - 22});
        this->addChild(menu, 10);

        auto spr = ButtonSprite::create("AutoGen", 70, true, "bigFont.fnt", "GJ_button_02.png", 28.f, 0.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(RGEditorUI::onGenerator));
        btn->setID("generator-button"_spr);
        menu->addChild(btn);
        m_fields->menu = menu;
        return true;
    }

    void showUI(bool show) {
        EditorUI::showUI(show);
        if (m_fields->menu) m_fields->menu->setVisible(show);
    }

    void onGenerator(CCObject*) {
        rgTrace("button: create popup");
        if (auto popup = GeneratorPopup::create(m_editorLayer)) {
            rgTrace("button: show popup");
            popup->show();
            rgTrace("button: shown");
        }
    }
};
