// setvalues <in.vst3state> <out.vst3state> name=value ...: a MuseScore setup state with a Kontakt script's saved values
// changed (KontaktSetup::withScriptValues, as [slurs] speedFromWritten does at load); own legato round 3 records with
// "$vfm12=0". Build: g++ -std=c++17 setvalues.cpp <fork>/audio/vst3/kontaktsetup.cpp -I<fork>/audio/vst3 $(pkg-config --cflags --libs Qt5Core)
#include "kontaktsetup.h"
#include <QDataStream>
#include <QFile>
#include <cstdio>
int main(int argc, char** argv)
{
    if (argc < 4) { fprintf(stderr, "usage\n"); return 2; }
    QFile f(argv[1]); f.open(QIODevice::ReadOnly); QByteArray st = f.readAll();
    if (!st.startsWith("MSV3")) return 3;
    QDataStream ds(st.mid(4)); quint32 v; QString name; QByteArray comp, ctl;
    ds >> v >> name >> comp >> ctl;
    std::map<QString, QByteArray> set;
    for (int i = 3; i < argc; ++i) { QString a = argv[i]; int e = a.indexOf('='); set[a.left(e)] = a.mid(e + 1).toUtf8(); }
    auto before = Ms::KontaktSetup::scriptValues(Ms::KontaktSetup::slotProgram(comp, nullptr));
    for (auto& p : set) printf("%s before %s\n", qPrintable(p.first), before.count(p.first) ? before[p.first].constData() : "(none)");
    QString err; int n = 0;
    QByteArray out = Ms::KontaktSetup::withScriptValues(comp, set, &err, &n);
    if (out.isEmpty() || n < 1) { fprintf(stderr, "failed %s (%d)\n", qPrintable(err), n); return 1; }
    auto after = Ms::KontaktSetup::scriptValues(Ms::KontaktSetup::slotProgram(out, nullptr));
    for (auto& p : set) printf("%s after %s\n", qPrintable(p.first), after[p.first].constData());
    QByteArray res("MSV3"); QDataStream o(&res, QIODevice::WriteOnly | QIODevice::Append);
    o << quint32(1) << name << out << ctl;
    QFile g(argv[2]); g.open(QIODevice::WriteOnly); g.write(res);
    printf("%d values set, %s\n", n, qPrintable(name));
}
