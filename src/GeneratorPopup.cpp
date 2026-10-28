#include "GeneratorPopup.hpp"

#include "Analysis.hpp"
#include "AudioDecode.hpp"
#include "Trace.hpp"

#include <random>
#include <thread>

using namespace geode::prelude;

// remembered between popups for the current game session
static struct {
    int difficulty = 2;
    bool clear = true;
    bool effects = true;
    int offsetMs = 0;
    uint32_t seed = 0;
} s_last;

namespace {
constexpr int TOUCH_PRIO = -500;
CCMenuItemSpriteExtra* textButton(char const* text, int width, char const* bg, float scale, CCObject* target, SEL_MenuHandler sel) {
    auto spr = ButtonSprite::create(text, width, true, "bigFont.fnt", bg, 28.f, scale);
    return CCMenuItemSpriteExtra::create(spr, target, sel);
}
} // namespace

GeneratorPopup* GeneratorPopup::create(LevelEditorLayer* editor) {
    auto ret = new GeneratorPopup();
    if (ret->init(editor)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

GeneratorPopup::~GeneratorPopup() {
    rgTrace("popup: destroyed");
}

bool GeneratorPopup::init(LevelEditorLayer* editor) {
    rgTrace("popup: initWithColor");
    if (!this->initWithColor({0, 0, 0, 120})) return false;
    m_editor = editor;
    if (!s_last.seed) s_last.seed = std::random_device{}() % 1000000;

    auto win = CCDirector::sharedDirector()->getWinSize();
    m_size = CCSize(390.f, 280.f);

    rgTrace("popup: background");
    m_main = CCLayer::create();
    m_main->ignoreAnchorPointForPosition(false);
    m_main->setContentSize(m_size);
    m_main->setAnchorPoint({0.5f, 0.5f});
    m_main->setPosition(win / 2);
    this->addChild(m_main);

    auto bg = extension::CCScale9Sprite::create("GJ_square01.png");
    bg->setContentSize(m_size);
    bg->setPosition(m_size / 2);
    m_main->addChild(bg);

    m_menu = CCMenu::create();
    m_menu->ignoreAnchorPointForPosition(false);
    m_menu->setContentSize(m_size);
    m_menu->setAnchorPoint({0, 0});
    m_menu->setPosition({0, 0});
    m_main->addChild(m_menu, 10);
    // set before the menu registers itself (on enter): handlers added during a touch are
    // queued by cocos, so changing the priority afterwards finds no handler and crashes
    m_menu->setTouchPriority(TOUCH_PRIO - 1);

    auto title = CCLabelBMFont::create("Rhythm Level Generator", "goldFont.fnt");
    title->setScale(0.75f);
    title->setPosition({m_size.width / 2, m_size.height - 20});
    m_main->addChild(title);

    auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    closeSpr->setScale(0.8f);
    auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(GeneratorPopup::onClose));
    closeBtn->setPosition({6, m_size.height - 6});
    m_menu->addChild(closeBtn);

    // --- song -------------------------------------------------------------------------
    rgTrace("popup: song row");
    m_songLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_songLabel->setPosition({m_size.width / 2, m_size.height - 50});
    m_main->addChild(m_songLabel);

    auto libBtn = textButton("Choose Song", 150, "GJ_button_02.png", 0.65f, this, menu_selector(GeneratorPopup::onLibrary));
    libBtn->setPosition({m_size.width / 2, m_size.height - 80});
    m_menu->addChild(libBtn);

    // --- difficulty ---------------------------------------------------------------------
    rgTrace("popup: difficulty row");
    auto diffTitle = CCLabelBMFont::create("Difficulty", "goldFont.fnt");
    diffTitle->setScale(0.55f);
    diffTitle->setPosition({m_size.width / 2, m_size.height - 110});
    m_main->addChild(diffTitle);

    float step = (m_size.width - 30) / rg::DIFFICULTY_COUNT;
    for (int i = 0; i < rg::DIFFICULTY_COUNT; i++) {
        auto spr = ButtonSprite::create(
            rg::difficultyName(rg::Difficulty(i)), 52, true, "bigFont.fnt", "GJ_button_04.png", 26.f, 0.45f
        );
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(GeneratorPopup::onDifficulty));
        btn->setTag(i);
        btn->setPosition({15 + step * (i + 0.5f), m_size.height - 135});
        m_menu->addChild(btn);
        m_diffSprites.push_back(spr);
    }

    // --- options (toggle buttons instead of checkboxes, seed and offset with arrows) ------
    rgTrace("popup: options row");
    auto clearBtn = textButton("Clear old gen", 95, "GJ_button_04.png", 0.45f, this, menu_selector(GeneratorPopup::onToggleClear));
    clearBtn->setTag(100);
    clearBtn->setPosition({70, 98});
    m_menu->addChild(clearBtn);
    auto fxBtn = textButton("Effects", 95, "GJ_button_04.png", 0.45f, this, menu_selector(GeneratorPopup::onToggleEffects));
    fxBtn->setTag(101);
    fxBtn->setPosition({70, 68});
    m_menu->addChild(fxBtn);

    m_seedLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_seedLabel->setPosition({250, 98});
    m_main->addChild(m_seedLabel);
    auto reroll = textButton("New", 40, "GJ_button_05.png", 0.45f, this, menu_selector(GeneratorPopup::onReroll));
    reroll->setPosition({345, 98});
    m_menu->addChild(reroll);

    m_offsetLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_offsetLabel->setPosition({250, 68});
    m_main->addChild(m_offsetLabel);
    for (int s : {-1, 1}) {
        auto b = textButton(s < 0 ? "-" : "+", 22, "GJ_button_05.png", 0.5f, this, menu_selector(GeneratorPopup::onOffset));
        b->setTag(s);
        b->setPosition({s < 0 ? 170.f : 345.f, 68});
        m_menu->addChild(b);
    }

    // --- generate ---------------------------------------------------------------------
    rgTrace("popup: generate button");
    auto genSpr = ButtonSprite::create("Generate", 160, true, "goldFont.fnt", "GJ_button_01.png", 34.f, 0.9f);
    auto genBtn = CCMenuItemSpriteExtra::create(genSpr, this, menu_selector(GeneratorPopup::onGenerate));
    genBtn->setPosition({m_size.width / 2, 35});
    m_menu->addChild(genBtn);

    m_status = CCLabelBMFont::create("", "chatFont.fnt");
    m_status->setPosition({m_size.width / 2, 12});
    m_main->addChild(m_status);

    refreshOptions();
    rgTrace("popup: refreshSongLabel");
    refreshSongLabel();
    setStatus("Choose a song and a difficulty, then press Generate");

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);
    rgTrace("popup: init done");
    return true;
}

void GeneratorPopup::show() {
    auto scene = CCDirector::sharedDirector()->getRunningScene();
    scene->addChild(this, 1000);
}

void GeneratorPopup::registerWithTouchDispatcher() {
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, TOUCH_PRIO, true);
}

bool GeneratorPopup::ccTouchBegan(CCTouch*, CCEvent*) {
    return this->isVisible(); // swallow everything behind the window (not while hidden)
}

void GeneratorPopup::keyBackClicked() {
    onClose(nullptr);
}

void GeneratorPopup::onClose(CCObject*) {
    if (m_busy) return;
    this->unscheduleAllSelectors();
    this->setKeypadEnabled(false);
    this->setTouchEnabled(false);
    this->removeFromParentAndCleanup(true);
}

void GeneratorPopup::setStatus(std::string const& text, ccColor3B color) {
    m_status->setString(text.c_str());
    m_status->setColor(color);
    m_status->limitLabelWidth(m_size.width - 20, 0.6f, 0.2f);
}

void GeneratorPopup::refreshOptions() {
    for (int i = 0; i < int(m_diffSprites.size()); i++) {
        m_diffSprites[i]->updateBGImage(i == s_last.difficulty ? "GJ_button_01.png" : "GJ_button_04.png");
    }
    auto setOn = [&](int tag, bool on) {
        if (auto btn = static_cast<CCMenuItemSpriteExtra*>(m_menu->getChildByTag(tag))) {
            if (auto spr = static_cast<ButtonSprite*>(btn->getNormalImage())) {
                spr->updateBGImage(on ? "GJ_button_01.png" : "GJ_button_04.png");
            }
        }
    };
    setOn(100, s_last.clear);
    setOn(101, s_last.effects);
    m_seedLabel->setString(fmt::format("Seed: {}", s_last.seed).c_str());
    m_seedLabel->limitLabelWidth(140, 0.45f, 0.2f);
    m_offsetLabel->setString(fmt::format("Offset: {} ms", s_last.offsetMs).c_str());
    m_offsetLabel->limitLabelWidth(140, 0.45f, 0.2f);
}

void GeneratorPopup::refreshSongLabel() {
    auto level = m_editor->m_level;
    rgTrace(fmt::format("song: level songID={} track={}", level->m_songID, level->m_audioTrack));
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
    if (songID > 0) {
        auto mdm = MusicDownloadManager::sharedState();
        if (!mdm->isSongDownloaded(songID)) mdm->downloadSong(songID);
    }
    refreshSongLabel();
}

void GeneratorPopup::onLibrary(CCObject*) {
    // GD's own song menu: song ID field, Newgrounds search, the 2.2 Music Library and NCS.
    rgTrace("library: CustomSongLayer");
    auto layer = CustomSongLayer::create(this);
    if (!layer) return;
    // GD shows it below our window, so step aside until it is closed
    this->setVisible(false);
    layer->show();
    m_songLayer = layer;
    this->schedule(schedule_selector(GeneratorPopup::checkSongLayer), 0.2f);
}

void GeneratorPopup::checkSongLayer(float) {
    if (m_songLayer && m_songLayer->getParent()) return;
    this->unschedule(schedule_selector(GeneratorPopup::checkSongLayer));
    m_songLayer = nullptr;
    this->setVisible(true);
    refreshSongLabel();
}

void GeneratorPopup::songIDChanged(int id) {
    rgTrace(fmt::format("library: songIDChanged {}", id));
    setSong(id);
    setStatus("Song selected");
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
    s_last.difficulty = sender->getTag();
    refreshOptions();
}

void GeneratorPopup::onToggleClear(CCObject*) {
    s_last.clear = !s_last.clear;
    refreshOptions();
}

void GeneratorPopup::onToggleEffects(CCObject*) {
    s_last.effects = !s_last.effects;
    refreshOptions();
}

void GeneratorPopup::onReroll(CCObject*) {
    s_last.seed = std::random_device{}() % 1000000;
    refreshOptions();
}

void GeneratorPopup::onOffset(CCObject* sender) {
    s_last.offsetMs = std::clamp(s_last.offsetMs + sender->getTag() * 10, -300, 300);
    refreshOptions();
}

void GeneratorPopup::onGenerate(CCObject*) {
    if (m_busy) return;
    rgTrace("generate: clicked");
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
        setStatus("Download failed. Try Choose Song again.", {255, 100, 100});
    }
}

void GeneratorPopup::startDecode(float) {
    auto level = m_editor->m_level;
    std::string err;
    rgTrace("generate: resolve path");
    auto path = resolveSongPath(level->m_songID, level->m_audioTrack, err);
    std::vector<float> samples;
    int rate = 0;
    rgTrace("generate: decode " + path);
    if (path.empty() || !decodeSongToMono(path, samples, rate, err)) {
        m_busy = false;
        setStatus(err, {255, 100, 100});
        return;
    }
    rgTrace(fmt::format("generate: decoded {} samples at {} Hz", samples.size(), rate));
    setStatus("Analyzing rhythm and planning the speeds...", {255, 220, 120});

    rg::GenOptions opt;
    opt.difficulty = rg::Difficulty(s_last.difficulty);
    opt.effects = s_last.effects;
    opt.seed = s_last.seed;
    opt.syncOffset = s_last.offsetMs / 1000.0;
    opt.songOffset = m_editor->m_levelSettings ? m_editor->m_levelSettings->m_songOffset : 0.0;
    opt.speedPortalsOnly = true;

    // pass 1 on a worker: analysis + the speed plan (only speed portals)
    m_job = std::make_shared<Job>();
    m_job->opt = opt;
    std::thread([job = m_job, samples = std::move(samples), rate]() mutable {
        job->analysis = rg::analyze(samples, rate);
        job->result = rg::generate(job->analysis, job->opt);
        job->done = true;
    }).detach();
    this->schedule(schedule_selector(GeneratorPopup::checkWorker), 0.1f);
}

void GeneratorPopup::checkWorker(float) {
    if (!m_job || !m_job->done) return;
    if (m_job->stage == 1) {
        // ---- pass 1 finished: place the speed portals, then ask GD where the player is
        rgTrace("generate: pass 1 finished");
        auto& p1 = m_job->result;
        if (p1.objects.empty()) {
            this->unschedule(schedule_selector(GeneratorPopup::checkWorker));
            auto job = std::move(m_job);
            applyResult(job->result);
            return;
        }
        if (s_last.clear) {
            // Let GD walk its own object list: reading m_objects through the fresh 2.2091
            // bindings returned garbage pointers and crashed in removeObject.
            for (int id : rg::generatedObjectIds()) m_editor->removeAllObjectsOfType(id);
        }
        rgTrace(fmt::format("generate: place {} speed portals", p1.objectCount));
        m_editor->createObjectsFromString(p1.objects, true, true);
        if (auto settings = m_editor->m_levelSettings) {
            settings->m_startSpeed = Speed(p1.startSpeed);
            settings->m_startMode = 0;
        }
        // GD's own time -> position conversion, with the portals that are now in the level
        rgTrace("generate: sample posForTime");
        double step = m_job->opt.timelineStep;
        std::vector<double> table;
        size_t n = p1.modelX.size();
        table.reserve(n);
        for (size_t i = 0; i < n; i++) table.push_back(m_editor->posForTime(float(i * step)).x);
        // sanity: increasing and close to the generator's own model (portal timing differs by
        // at most a few dozen units; anything else means the table can't be trusted)
        bool ok = n > 10;
        double maxDiff = 0;
        for (size_t i = 1; ok && i < n; i++) {
            if (!(table[i] >= table[i - 1]) || !std::isfinite(table[i])) ok = false;
            maxDiff = std::max(maxDiff, std::abs(table[i] - p1.modelX[i]));
        }
        if (ok && maxDiff > 150) ok = false;
        rgTrace(fmt::format("generate: GD timeline {} (max diff to model {:.1f} units)", ok ? "used" : "rejected", maxDiff));
        auto opt2 = m_job->opt;
        opt2.speedPortalsOnly = false;
        if (ok) opt2.timelineX = std::move(table);
        else {
            // fall back to the model; pass 2 then places the speed portals itself
            for (int id : {200, 201, 202, 203, 1334}) m_editor->removeAllObjectsOfType(id);
        }
        m_syncNote = ok ? fmt::format("synced to GD timeline (portal offset {:.0f} units)", maxDiff)
                        : std::string("GD timeline unavailable, model timing");
        // ---- pass 2 on a worker: everything else on that timeline
        setStatus("Building the level on GD's timeline...", {255, 220, 120});
        auto job = m_job;
        job->done = false;
        job->stage = 2;
        job->opt = std::move(opt2);
        std::thread([job]() {
            job->result = rg::generate(job->analysis, job->opt);
            job->done = true;
        }).detach();
        return;
    }
    this->unschedule(schedule_selector(GeneratorPopup::checkWorker));
    auto job = std::move(m_job);
    rgTrace("generate: pass 2 finished");
    applyResult(job->result);
}

void GeneratorPopup::applyResult(rg::GenResult const& res) {
    m_busy = false;
    if (res.objects.empty()) {
        setStatus(res.summary.empty() ? "Generation failed" : res.summary, {255, 100, 100});
        return;
    }
    rgTrace(fmt::format("apply: createObjectsFromString ({} objects)", res.objectCount));
    m_editor->createObjectsFromString(res.objects, true, true);
    rgTrace("apply: done");
    setStatus(fmt::format("Done! {} | {}", m_syncNote, res.summary), {120, 255, 120});
}
