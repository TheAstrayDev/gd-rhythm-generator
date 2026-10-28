#include "GeneratorPopup.hpp"

#include "Analysis.hpp"
#include "AudioDecode.hpp"

#include <random>
#include <thread>

using namespace geode::prelude;

// remembered between popups for the current game session
static struct { int difficulty = 2; bool clear = true; bool effects = true; } s_last;

GeneratorPopup* GeneratorPopup::create(LevelEditorLayer* editor) {
    auto ret = new GeneratorPopup();
    if (ret->init(editor)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool GeneratorPopup::init(LevelEditorLayer* editor) {
    if (!Popup::init(390.f, 280.f)) return false;
    m_editor = editor;
    this->setTitle("Rhythm Level Generator");

    auto const size = m_size;
    m_difficulty = s_last.difficulty;
    m_effects = s_last.effects;
    m_clear = s_last.clear;

    // --- song -------------------------------------------------------------------------
    m_songLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_songLabel->setPosition({size.width / 2, size.height - 52});
    m_mainLayer->addChild(m_songLabel);

    m_songInput = TextInput::create(110.f, "Song ID");
    m_songInput->setCommonFilter(CommonFilter::Uint);
    m_songInput->setPosition({80, size.height - 85});
    m_songInput->setScale(0.85f);
    m_mainLayer->addChild(m_songInput);

    auto setBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Set", 40, true, "bigFont.fnt", "GJ_button_01.png", 28.f, 0.6f), this,
        menu_selector(GeneratorPopup::onSetSong)
    );
    setBtn->setPosition({165, size.height - 85});
    m_buttonMenu->addChild(setBtn);

    auto libBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Song Library", 120, true, "bigFont.fnt", "GJ_button_02.png", 28.f, 0.6f), this,
        menu_selector(GeneratorPopup::onLibrary)
    );
    libBtn->setPosition({290, size.height - 85});
    m_buttonMenu->addChild(libBtn);

    // --- difficulty ---------------------------------------------------------------------
    auto diffTitle = CCLabelBMFont::create("Difficulty", "goldFont.fnt");
    diffTitle->setScale(0.55f);
    diffTitle->setPosition({size.width / 2, size.height - 117});
    m_mainLayer->addChild(diffTitle);

    float step = (size.width - 30) / rg::DIFFICULTY_COUNT;
    for (int i = 0; i < rg::DIFFICULTY_COUNT; i++) {
        auto spr = ButtonSprite::create(
            rg::difficultyName(rg::Difficulty(i)), 52, true, "bigFont.fnt", "GJ_button_04.png", 26.f, 0.45f
        );
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(GeneratorPopup::onDifficulty));
        btn->setTag(i);
        btn->setPosition({15 + step * (i + 0.5f), size.height - 142});
        m_buttonMenu->addChild(btn);
        m_diffSprites.push_back(spr);
    }
    for (int i = 0; i < rg::DIFFICULTY_COUNT; i++) {
        m_diffSprites[i]->updateBGImage(i == m_difficulty ? "GJ_button_01.png" : "GJ_button_04.png");
    }

    // --- options ----------------------------------------------------------------------
    auto addToggle = [&](char const* text, float x, bool on, SEL_MenuHandler sel) {
        auto tog = CCMenuItemToggler::createWithStandardSprites(this, sel, 0.6f);
        tog->toggle(on);
        tog->setPosition({x, 82});
        m_buttonMenu->addChild(tog);
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.38f);
        lbl->setAnchorPoint({0, 0.5f});
        lbl->setPosition({x + 16, 82});
        m_mainLayer->addChild(lbl);
    };
    addToggle("Clear level", 30, m_clear, menu_selector(GeneratorPopup::onToggleClear));
    addToggle("Effects", 150, m_effects, menu_selector(GeneratorPopup::onToggleEffects));

    m_seedInput = TextInput::create(110.f, "Seed");
    m_seedInput->setCommonFilter(CommonFilter::Uint);
    m_seedInput->setScale(0.75f);
    m_seedInput->setPosition({315, 82});
    m_seedInput->setString(std::to_string(std::random_device{}() % 1000000));
    m_mainLayer->addChild(m_seedInput);

    // --- generate ---------------------------------------------------------------------
    auto genBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Generate", 160, true, "goldFont.fnt", "GJ_button_01.png", 34.f, 0.9f), this,
        menu_selector(GeneratorPopup::onGenerate)
    );
    genBtn->setPosition({size.width / 2, 45});
    m_buttonMenu->addChild(genBtn);

    m_status = CCLabelBMFont::create("", "chatFont.fnt");
    m_status->setPosition({size.width / 2, 16});
    m_mainLayer->addChild(m_status);

    auto level = m_editor->m_level;
    m_songInput->setString(level->m_songID > 0 ? std::to_string(level->m_songID) : "");
    refreshSongLabel();
    setStatus("Pick a song, a difficulty and press Generate");
    return true;
}

void GeneratorPopup::setStatus(std::string const& text, ccColor3B color) {
    m_status->setString(text.c_str());
    m_status->setColor(color);
    m_status->limitLabelWidth(m_size.width - 20, 0.6f, 0.2f);
}

void GeneratorPopup::refreshSongLabel() {
    auto level = m_editor->m_level;
    std::string name = songDisplayName(level->m_songID, level->m_audioTrack);
    std::string err;
    bool ready = !resolveSongPath(level->m_songID, level->m_audioTrack, err).empty();
    m_songLabel->setString(name.c_str());
    m_songLabel->setColor(ready ? ccColor3B{120, 255, 120} : ccColor3B{255, 200, 90});
    m_songLabel->limitLabelWidth(m_size.width - 30, 0.5f, 0.15f);
}

void GeneratorPopup::setSong(int songID) {
    auto level = m_editor->m_level;
    level->m_songID = std::max(0, songID);
    m_songInput->setString(songID > 0 ? std::to_string(songID) : "");
    if (songID > 0) {
        auto mdm = MusicDownloadManager::sharedState();
        mdm->getSongInfo(songID, false);
        if (!mdm->isSongDownloaded(songID)) mdm->downloadSong(songID);
    }
    refreshSongLabel();
}

void GeneratorPopup::onSetSong(CCObject*) {
    int id = numFromString<int>(std::string(m_songInput->getString())).unwrapOr(0);
    setSong(id);
    setStatus(id > 0 ? "Song set (downloading if needed)" : "Using the level's main track");
}

void GeneratorPopup::onLibrary(CCObject*) {
    // GD's own song menu: Newgrounds search, the 2.2 Music Library and NCS browsers.
    if (auto layer = CustomSongLayer::create(this)) layer->show();
}

void GeneratorPopup::songIDChanged(int id) {
    setSong(id);
    setStatus("Song selected from the library");
}

int GeneratorPopup::getActiveSongID() {
    return m_editor->m_level->m_songID;
}

std::string GeneratorPopup::getSongFileName() {
    auto id = m_editor->m_level->m_songID;
    return id > 0 ? std::string(MusicDownloadManager::sharedState()->pathForSong(id)) : "";
}

LevelSettingsObject* GeneratorPopup::getLevelSettings() {
    return m_editor->m_levelSettings;
}

void GeneratorPopup::onDifficulty(CCObject* sender) {
    m_difficulty = sender->getTag();
    for (int i = 0; i < int(m_diffSprites.size()); i++) {
        m_diffSprites[i]->updateBGImage(i == m_difficulty ? "GJ_button_01.png" : "GJ_button_04.png");
    }
    s_last.difficulty = m_difficulty;
}

void GeneratorPopup::onToggleClear(CCObject*) {
    m_clear = !m_clear;
    s_last.clear = m_clear;
}

void GeneratorPopup::onToggleEffects(CCObject*) {
    m_effects = !m_effects;
    s_last.effects = m_effects;
}

void GeneratorPopup::onClose(CCObject*) {
    this->unscheduleAllSelectors();
    this->setKeypadEnabled(false);
    this->setTouchEnabled(false);
    this->removeFromParentAndCleanup(true);
}

void GeneratorPopup::onGenerate(CCObject*) {
    if (m_busy) return;
    auto level = m_editor->m_level;
    std::string err;
    if (resolveSongPath(level->m_songID, level->m_audioTrack, err).empty()) {
        if (level->m_songID > 0) {
            MusicDownloadManager::sharedState()->downloadSong(level->m_songID);
            m_busy = true;
            m_downloadWait = 0;
            setStatus("Downloading song...", {255, 220, 120});
            this->schedule(schedule_selector(GeneratorPopup::checkDownload), 0.5f);
            return;
        }
        setStatus(err, {255, 100, 100});
        return;
    }
    m_busy = true;
    setStatus("Decoding song...", {255, 220, 120});
    // let the label render before the (blocking) decode
    this->scheduleOnce(schedule_selector(GeneratorPopup::startDecode), 0.05f);
}

void GeneratorPopup::checkDownload(float dt) {
    auto level = m_editor->m_level;
    m_downloadWait += dt;
    std::string err;
    if (!resolveSongPath(level->m_songID, level->m_audioTrack, err).empty()) {
        this->unschedule(schedule_selector(GeneratorPopup::checkDownload));
        refreshSongLabel();
        setStatus("Decoding song...", {255, 220, 120});
        this->scheduleOnce(schedule_selector(GeneratorPopup::startDecode), 0.05f);
    } else if (m_downloadWait > 60) {
        this->unschedule(schedule_selector(GeneratorPopup::checkDownload));
        m_busy = false;
        setStatus("Download failed. Try the Song Library button.", {255, 100, 100});
    }
}

void GeneratorPopup::startDecode(float) {
    auto level = m_editor->m_level;
    std::string err;
    auto path = resolveSongPath(level->m_songID, level->m_audioTrack, err);
    std::vector<float> samples;
    int rate = 0;
    if (path.empty() || !decodeSongToMono(path, samples, rate, err)) {
        m_busy = false;
        setStatus(err, {255, 100, 100});
        return;
    }
    setStatus("Analyzing rhythm and building the level...", {255, 220, 120});

    rg::GenOptions opt;
    opt.difficulty = rg::Difficulty(m_difficulty);
    opt.effects = m_effects;
    opt.seed = uint32_t(numFromString<uint64_t>(std::string(m_seedInput->getString())).unwrapOr(1));
    opt.syncOffset = double(Mod::get()->getSettingValue<int64_t>("offset-ms")) / 1000.0;
    opt.songOffset = m_editor->m_levelSettings ? m_editor->m_levelSettings->m_songOffset : 0.0;

    std::thread([self = Ref<GeneratorPopup>(this), samples = std::move(samples), rate, opt]() mutable {
        auto analysis = rg::analyze(samples, rate);
        auto result = rg::generate(analysis, opt);
        queueInMainThread([self = std::move(self), result = std::move(result)]() mutable {
            self->applyResult(result);
        });
    }).detach();
}

void GeneratorPopup::applyResult(rg::GenResult const& res) {
    m_busy = false;
    if (LevelEditorLayer::get() != m_editor) return; // editor was closed meanwhile
    if (res.objects.empty()) {
        setStatus(res.summary.empty() ? "Generation failed" : res.summary, {255, 100, 100});
        return;
    }
    if (m_clear) {
        std::vector<GameObject*> old;
        for (auto obj : CCArrayExt<GameObject*>(m_editor->m_objects)) old.push_back(obj);
        for (auto obj : old) m_editor->removeObject(obj, true);
    }
    m_editor->createObjectsFromString(res.objects, true, true);
    if (auto settings = m_editor->m_levelSettings) {
        settings->m_startSpeed = Speed(res.startSpeed);
        settings->m_startMode = 0;
    }
    if (auto ui = EditorUI::get()) ui->updateButtons();

    log::info("Generated: {}", res.summary);
    setStatus(res.summary, {120, 255, 120});
    Notification::create(fmt::format("Level generated: {} objects", res.objectCount), NotificationIcon::Success)->show();
}
