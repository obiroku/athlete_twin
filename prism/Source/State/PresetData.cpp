#include "PresetData.h"
#include "../DSP/Wavetable.h"
#include "../DSP/FactoryTables.h"

namespace prism
{
PresetData PresetData::init()
{
    PresetData p;
    const auto& d = paramDefs();
    for (int i = 0; i < P::NumParams; ++i) p.params[(size_t) i] = d[(size_t) i].def;
    // Default routings (SPEC §6): Env 2 -> F1 cutoff (depth 0), aftertouch -> F1 cutoff (+0.1)
    auto& r0 = p.extras.routings[0];
    r0.src = (int16_t) Src::Env2; r0.dest = (int16_t) P::filt (0, P::f_cutoff);
    auto& r1 = p.extras.routings[1];
    r1.src = (int16_t) Src::ChanAT; r1.dest = (int16_t) P::filt (0, P::f_cutoff);
    p.params[(size_t) P::modDepth (0)] = 0.0f;
    p.params[(size_t) P::modDepth (1)] = 0.1f;
    p.tables[0].factory = "Basic Shapes";
    p.tables[1].factory = "Basic Shapes";
    return p;
}

// ------------------------------------------------------------------ helpers

juce::String encodeFrames (const std::vector<std::vector<float>>& frames)
{
    juce::MemoryOutputStream mo;
    for (auto& f : frames)
        for (int i = 0; i < dsp::Wavetable::N; ++i)
            mo.writeFloat (i < (int) f.size() ? f[(size_t) i] : 0.0f); // little endian
    return juce::Base64::toBase64 (mo.getData(), mo.getDataSize());
}

std::vector<std::vector<float>> decodeFrames (const juce::String& b64, int numFrames)
{
    juce::MemoryOutputStream mo;
    if (! juce::Base64::convertFromBase64 (mo, b64)) return {};
    const int total = (int) (mo.getDataSize() / sizeof (float));
    const int N = dsp::Wavetable::N;
    numFrames = std::min (numFrames > 0 ? numFrames : total / N, total / N);
    numFrames = std::min (numFrames, dsp::Wavetable::MaxFrames);
    std::vector<std::vector<float>> frames ((size_t) std::max (0, numFrames), std::vector<float> ((size_t) N));
    juce::MemoryInputStream mi (mo.getData(), mo.getDataSize(), false);
    for (auto& f : frames)
        for (int i = 0; i < N; ++i)
        {
            float v = mi.readFloat();
            f[(size_t) i] = std::isfinite (v) ? v : 0.0f;
        }
    return frames;
}

static juce::var shapeToVar (const dsp::Shape& s)
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> pts;
    for (int i = 0; i < s.count; ++i)
    {
        juce::Array<juce::var> p;
        p.add (s.pts[(size_t) i].x); p.add (s.pts[(size_t) i].y); p.add (s.pts[(size_t) i].curve);
        pts.add (p);
    }
    o->setProperty ("points", pts);
    o->setProperty ("loop_start", s.loopStart);
    o->setProperty ("loop_end", s.loopEnd);
    return juce::var (o);
}

static void shapeFromVar (const juce::var& v, dsp::Shape& s)
{
    if (! v.isObject()) return;
    if (auto* pts = v["points"].getArray())
    {
        s.count = 0;
        for (auto& p : *pts)
        {
            if (s.count >= dsp::Shape::MaxPoints) break;
            if (auto* a = p.getArray(); a != nullptr && a->size() >= 2)
            {
                auto& sp = s.pts[(size_t) s.count++];
                sp.x = (float) (double) (*a)[0];
                sp.y = (float) (double) (*a)[1];
                sp.curve = a->size() > 2 ? (float) (double) (*a)[2] : 0.0f;
            }
        }
    }
    s.loopStart = (int) v.getProperty ("loop_start", s.loopStart);
    s.loopEnd = (int) v.getProperty ("loop_end", s.loopEnd);
    s.sanitize();
}

static juce::var tableToVar (const TableSource& t)
{
    auto* o = new juce::DynamicObject();
    if (! t.factory.empty()) o->setProperty ("factory", juce::String (t.factory));
    else
    {
        o->setProperty ("embedded", encodeFrames (t.frames));
        o->setProperty ("frames", (int) t.frames.size());
        o->setProperty ("name", juce::String (t.name));
    }
    return juce::var (o);
}

static void tableFromVar (const juce::var& v, TableSource& t)
{
    if (! v.isObject()) return;
    if (v.hasProperty ("factory"))
    {
        t = {};
        t.factory = v["factory"].toString().toStdString();
        if (dsp::factoryTableIndex (t.factory) < 0) t.factory = "Basic Shapes";
    }
    else if (v.hasProperty ("embedded"))
    {
        t = {};
        t.name = v.getProperty ("name", "User").toString().toStdString();
        t.frames = decodeFrames (v["embedded"].toString(), (int) v.getProperty ("frames", 0));
        if (t.frames.empty()) t.factory = "Basic Shapes";
    }
}

// ------------------------------------------------------------------ write

juce::var presetToJson (const PresetData& p, bool includeGuiScale)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format_version", kFormatVersion);
    o->setProperty ("name", juce::String (p.name));
    o->setProperty ("author", juce::String (p.author));
    o->setProperty ("category", juce::String (p.category));
    juce::Array<juce::var> tags;
    for (auto& t : p.tags) tags.add (juce::String (t));
    o->setProperty ("tags", tags);
    o->setProperty ("description", juce::String (p.description));

    juce::Array<juce::var> macros;
    for (int m = 0; m < 4; ++m)
    {
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("name", juce::String (p.macroNames[(size_t) m]));
        mo->setProperty ("value", p.params[(size_t) P::macro (m)]);
        macros.add (juce::var (mo));
    }
    o->setProperty ("macros", macros);

    auto* params = new juce::DynamicObject();
    const auto& defs = paramDefs();
    for (int i = 0; i < P::NumParams; ++i)
        params->setProperty (juce::Identifier (defs[(size_t) i].id), p.params[(size_t) i]);
    o->setProperty ("params", juce::var (params));

    juce::Array<juce::var> routings;
    for (int s = 0; s < P::NumModSlots; ++s)
    {
        const auto& r = p.extras.routings[(size_t) s];
        if (r.src < 0 || r.dest < 0) continue;
        auto* ro = new juce::DynamicObject();
        ro->setProperty ("slot", s + 1);
        ro->setProperty ("source", juce::String (srcId (r.src)));
        ro->setProperty ("dest", juce::String (defs[(size_t) r.dest].id));
        ro->setProperty ("depth", p.params[(size_t) P::modDepth (s)]);
        ro->setProperty ("polarity", juce::String (r.bipolar < 0 ? "native" : (r.bipolar ? "bi" : "uni")));
        ro->setProperty ("curve", r.curve);
        ro->setProperty ("aux", juce::String (r.aux >= 0 ? srcId (r.aux) : "none"));
        ro->setProperty ("bypass", r.bypass != 0);
        routings.add (juce::var (ro));
    }
    o->setProperty ("mod_routings", routings);

    juce::Array<juce::var> order;
    for (auto f : p.extras.fxOrder) order.add (juce::String (fxId (f)));
    o->setProperty ("fx_order", order);

    auto* wt = new juce::DynamicObject();
    wt->setProperty ("oscA", tableToVar (p.tables[0]));
    wt->setProperty ("oscB", tableToVar (p.tables[1]));
    o->setProperty ("wavetables", juce::var (wt));

    o->setProperty ("mseg", shapeToVar (p.extras.mseg));
    juce::Array<juce::var> lfoShapes;
    for (auto& s : p.extras.lfoCustom) lfoShapes.add (shapeToVar (s));
    o->setProperty ("lfo_custom_shapes", lfoShapes);
    juce::Array<juce::var> remaps;
    for (auto& s : p.extras.remap) remaps.add (shapeToVar (s));
    o->setProperty ("warp_remap", remaps);

    auto* arp = new juce::DynamicObject();
    juce::Array<juce::var> acc;
    for (auto a : p.extras.arpAccents) acc.add (a);
    arp->setProperty ("accents", acc);
    o->setProperty ("arp", juce::var (arp));

    auto* ch = new juce::DynamicObject();
    juce::Array<juce::var> notes;
    for (int i = 0; i < p.extras.chord.count; ++i) notes.add ((int) p.extras.chord.intervals[(size_t) i]);
    ch->setProperty ("notes", notes);
    o->setProperty ("chord", juce::var (ch));

    auto* midi = new juce::DynamicObject();
    midi->setProperty ("cc_a", p.extras.ccA);
    midi->setProperty ("cc_b", p.extras.ccB);
    o->setProperty ("midi", juce::var (midi));

    if (includeGuiScale) o->setProperty ("gui_scale", p.guiScale);
    return juce::var (o);
}

juce::String presetToString (const PresetData& p, bool includeGuiScale)
{
    return juce::JSON::toString (presetToJson (p, includeGuiScale), false, 6);
}

// ------------------------------------------------------------------ read

int migratePreset (juce::DynamicObject& obj)
{
    int v = (int) obj.getProperty ("format_version");
    if (v <= 0) v = 1; // files written before the field existed are v1
    // Chain: while (v < kFormatVersion) { switch (v) { case 1: migrate_1_to_2 (obj); break; ... } ++v; }
    while (v < kFormatVersion)
    {
        switch (v)
        {
            default: break; // no migrations yet
        }
        ++v;
    }
    obj.setProperty ("format_version", v);
    return v;
}

bool presetFromJson (const juce::var& in, PresetData& out, juce::String& error)
{
    auto* obj = in.getDynamicObject();
    if (obj == nullptr) { error = "not a JSON object"; return false; }
    if ((int) obj->getProperty ("format_version") > kFormatVersion) { error = "preset is from a newer PRISM"; return false; }
    // work on a deep copy so migration never mutates the caller's data
    juce::var copy = in.clone();
    auto* o = copy.getDynamicObject();
    migratePreset (*o);

    PresetData p = PresetData::init();
    // Init defaults for params, but routings start empty when the file lists its own
    p.name = o->getProperty ("name").toString().toStdString();
    if (p.name.empty()) p.name = "Untitled";
    p.author = o->getProperty ("author").toString().toStdString();
    p.category = o->getProperty ("category").toString().toStdString();
    p.description = o->getProperty ("description").toString().toStdString();
    if (auto* tags = o->getProperty ("tags").getArray())
        for (auto& t : *tags) p.tags.push_back (t.toString().toStdString());

    const auto& defs = paramDefs();
    const auto params = o->getProperty ("params");
    if (auto* po = params.getDynamicObject())
        for (auto& nv : po->getProperties())
        {
            const int idx = paramIndex (nv.name.toString().toStdString());
            if (idx < 0) continue; // unknown/deprecated ids are ignored
            const double v = (double) nv.value;
            if (std::isfinite (v)) p.params[(size_t) idx] = defs[(size_t) idx].snap ((float) v);
        }

    if (auto* macros = o->getProperty ("macros").getArray())
        for (int m = 0; m < std::min (4, macros->size()); ++m)
        {
            const auto& mv = (*macros)[m];
            p.macroNames[(size_t) m] = mv.getProperty ("name", juce::String ("Macro ") + juce::String (m + 1)).toString().toStdString();
            if (mv.hasProperty ("value") && ! (params.isObject() && params.hasProperty (juce::Identifier (defs[(size_t) P::macro (m)].id))))
                p.params[(size_t) P::macro (m)] = juce::jlimit (0.0f, 1.0f, (float) (double) mv["value"]);
        }

    if (o->hasProperty ("mod_routings"))
    {
        for (auto& r : p.extras.routings) r = Routing();
        if (auto* rs = o->getProperty ("mod_routings").getArray())
        {
            int autoSlot = 0;
            for (auto& rv : *rs)
            {
                int slot = (int) rv.getProperty ("slot", 0) - 1;
                if (slot < 0 || slot >= P::NumModSlots)
                {
                    while (autoSlot < P::NumModSlots && p.extras.routings[(size_t) autoSlot].src >= 0) ++autoSlot;
                    slot = autoSlot;
                }
                if (slot >= P::NumModSlots) break;
                Routing r;
                r.src = (int16_t) srcFromId (rv["source"].toString().toStdString());
                r.dest = (int16_t) paramIndex (rv["dest"].toString().toStdString());
                if (r.src < 0 || r.dest < 0 || ! defs[(size_t) r.dest].isModDest()) continue;
                const auto pol = rv.getProperty ("polarity", "native").toString();
                r.bipolar = (int8_t) (pol == "bi" ? 1 : (pol == "uni" ? 0 : -1));
                r.curve = juce::jlimit (-1.0f, 1.0f, (float) (double) rv.getProperty ("curve", 0.0));
                r.aux = (int16_t) srcFromId (rv.getProperty ("aux", "none").toString().toStdString());
                r.bypass = (int8_t) ((bool) rv.getProperty ("bypass", false) ? 1 : 0);
                p.extras.routings[(size_t) slot] = r;
                const auto depthId = juce::Identifier (defs[(size_t) P::modDepth (slot)].id);
                if (! (params.isObject() && params.hasProperty (depthId)) && rv.hasProperty ("depth"))
                    p.params[(size_t) P::modDepth (slot)] = juce::jlimit (-1.0f, 1.0f, (float) (double) rv["depth"]);
            }
        }
    }

    if (auto* order = o->getProperty ("fx_order").getArray())
    {
        std::array<int8_t, NumFx> fo {};
        std::array<bool, NumFx> seen {};
        int n = 0;
        for (auto& f : *order)
        {
            const int id = fxFromId (f.toString().toStdString());
            if (id < 0 || seen[(size_t) id] || n >= NumFx) continue;
            seen[(size_t) id] = true;
            fo[(size_t) n++] = (int8_t) id;
        }
        for (int i = 0; i < NumFx; ++i) if (! seen[(size_t) i]) fo[(size_t) n++] = (int8_t) i;
        p.extras.fxOrder = fo;
    }

    const auto wt = o->getProperty ("wavetables");
    tableFromVar (wt["oscA"], p.tables[0]);
    tableFromVar (wt["oscB"], p.tables[1]);

    shapeFromVar (o->getProperty ("mseg"), p.extras.mseg);
    if (auto* ls = o->getProperty ("lfo_custom_shapes").getArray())
        for (int i = 0; i < std::min (4, ls->size()); ++i) shapeFromVar ((*ls)[i], p.extras.lfoCustom[(size_t) i]);
    if (auto* rm = o->getProperty ("warp_remap").getArray())
        for (int i = 0; i < std::min (2, rm->size()); ++i) shapeFromVar ((*rm)[i], p.extras.remap[(size_t) i]);

    if (auto* acc = o->getProperty ("arp")["accents"].getArray())
        for (int i = 0; i < std::min (16, acc->size()); ++i) p.extras.arpAccents[(size_t) i] = juce::jlimit (0.0f, 1.0f, (float) (double) (*acc)[i]);
    if (auto* notes = o->getProperty ("chord")["notes"].getArray())
    {
        p.extras.chord.count = std::min (8, notes->size());
        for (int i = 0; i < p.extras.chord.count; ++i) p.extras.chord.intervals[(size_t) i] = (int8_t) juce::jlimit (-48, 48, (int) (*notes)[i]);
    }
    const auto midi = o->getProperty ("midi");
    p.extras.ccA = juce::jlimit (0, 127, (int) midi.getProperty ("cc_a", p.extras.ccA));
    p.extras.ccB = juce::jlimit (0, 127, (int) midi.getProperty ("cc_b", p.extras.ccB));
    p.guiScale = juce::jlimit (0.75, 2.0, (double) o->getProperty ("gui_scale").operator double() > 0 ? (double) o->getProperty ("gui_scale") : 1.0);

    out = std::move (p);
    return true;
}

bool presetFromString (const juce::String& s, PresetData& out, juce::String& error)
{
    juce::var v;
    const auto res = juce::JSON::parse (s, v);
    if (res.failed()) { error = res.getErrorMessage(); return false; }
    return presetFromJson (v, out, error);
}
} // namespace prism
