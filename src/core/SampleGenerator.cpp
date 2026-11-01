#include "SampleGenerator.h"

#include <QSaveFile>

#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>

namespace csv {

namespace {

// Small fast random generator; seeded per row, so every row is reproducible.
struct Rng {
    quint64 s;
    quint64 next()
    {
        quint64 z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int below(int n) { return int(next() % quint64(n)); }
    double unit() { return double(next() >> 11) / double(1ull << 53); }
};

struct Name { const char *text; const char *ascii; };   // ascii form for e-mail addresses

const Name kFirst[] = {
    {"Anna", "anna"}, {"Lukas", "lukas"}, {"Sophie", "sophie"}, {"Jonas", "jonas"}, {"Marie", "marie"},
    {"Felix", "felix"}, {"Emma", "emma"}, {"Leon", "leon"}, {"Mia", "mia"}, {"Paul", "paul"},
    {"Lena", "lena"}, {"Noah", "noah"}, {"Hannah", "hannah"}, {"Elias", "elias"}, {"Laura", "laura"},
    {"Minh", "minh"}, {"Linh", "linh"}, {"Tuấn", "tuan"}, {"Chloé", "chloe"}, {"Lucas", "lucas"},
    {"Giulia", "giulia"}, {"Marco", "marco"}, {"Olivia", "olivia"}, {"James", "james"}, {"Zofia", "zofia"},
    {"Jakub", "jakub"}, {"Lucía", "lucia"}, {"Mateo", "mateo"}, {"Sarah", "sarah"}, {"David", "david"},
    {"Julia", "julia"}, {"Max", "max"}};
const Name kLast[] = {
    {"Müller", "mueller"}, {"Schmidt", "schmidt"}, {"Schneider", "schneider"}, {"Fischer", "fischer"},
    {"Weber", "weber"}, {"Meyer", "meyer"}, {"Wagner", "wagner"}, {"Becker", "becker"}, {"Schulz", "schulz"},
    {"Hoffmann", "hoffmann"}, {"Nguyễn", "nguyen"}, {"Trần", "tran"}, {"Phạm", "pham"}, {"Dubois", "dubois"},
    {"Martin", "martin"}, {"Rossi", "rossi"}, {"Bianchi", "bianchi"}, {"Smith", "smith"}, {"Johnson", "johnson"},
    {"Kowalski", "kowalski"}, {"Nowak", "nowak"}, {"García", "garcia"}, {"López", "lopez"}, {"Jansen", "jansen"},
    {"de Vries", "devries"}, {"Huber", "huber"}, {"Gruber", "gruber"}, {"Keller", "keller"}, {"Weiß", "weiss"},
    {"Schäfer", "schaefer"}};

struct Country { const char *name; int weight; std::array<const char *, 6> cities; int cityCount; };
const Country kCountries[] = {
    {"Germany", 40, {"Berlin", "Hamburg", "München", "Köln", "Frankfurt am Main", "Düsseldorf"}, 6},
    {"Austria", 10, {"Wien", "Graz", "Salzburg", "Linz"}, 4},
    {"Switzerland", 8, {"Zürich", "Genève", "Basel", "Bern"}, 4},
    {"Netherlands", 9, {"Amsterdam", "Rotterdam", "Utrecht"}, 3},
    {"France", 10, {"Paris", "Lyon", "Marseille", "Toulouse"}, 4},
    {"Italy", 9, {"Milano", "Roma", "Torino"}, 3},
    {"Poland", 7, {"Warszawa", "Kraków", "Gdańsk"}, 3},
    {"Spain", 7, {"Madrid", "Barcelona", "Valencia"}, 3}};

struct Product { const char *name; const char *category; double price; };
const Product kProducts[] = {
    {"Wireless Mouse", "Electronics", 24.99}, {"Mechanical Keyboard", "Electronics", 89.00},
    {"USB-C Charger 65W", "Electronics", 34.90}, {"Noise Cancelling Headphones", "Electronics", 199.00},
    {"27\" Monitor", "Electronics", 249.00}, {"Webcam HD", "Electronics", 59.90},
    {"Coffee Grinder", "Home & Kitchen", 64.90}, {"Espresso Cups (Set of 4)", "Home & Kitchen", 19.95},
    {"Chef's Knife", "Home & Kitchen", 49.00}, {"Cast Iron Pan", "Home & Kitchen", 39.99},
    {"LED Desk Lamp", "Home & Kitchen", 39.99}, {"Yoga Mat", "Sports", 29.95},
    {"Running Shoes", "Sports", 119.00}, {"Water Bottle 1L", "Sports", 14.99},
    {"Cycling Helmet", "Sports", 79.00}, {"Travel Backpack 30L", "Bags", 89.00},
    {"Laptop Sleeve 14\"", "Bags", 24.90}, {"Notebook A5 (dotted)", "Office", 9.90},
    {"Fountain Pen", "Office", 34.00}, {"Desk Organizer", "Office", 22.50},
    {"Board Game: City Builders", "Toys & Games", 39.90}, {"Puzzle 1000 pieces", "Toys & Games", 17.99},
    {"Plant Pot (ceramic)", "Garden", 15.90}, {"Garden Hose 20m", "Garden", 32.00}};

const char *kChannels[] = {"Web shop", "Web shop", "Web shop", "Mobile app", "Mobile app", "Marketplace", "Phone"};
const char *kPayments[] = {"Credit card", "PayPal", "PayPal", "Invoice", "SEPA direct debit", "Klarna"};

void appendInt(std::string &s, qint64 v, int width = 0)
{
    char buf[24];
    std::snprintf(buf, sizeof buf, "%0*lld", width, static_cast<long long>(v));
    s += buf;
}

void appendMoney(std::string &s, double v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    s += buf;
}

// Days since 1970-01-01 -> "YYYY-MM-DD" (Howard Hinnant's civil_from_days).
void appendDate(std::string &s, qint64 days)
{
    days += 719468;
    const qint64 era = days / 146097;
    const qint64 doe = days - era * 146097;
    const qint64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    qint64 y = yoe + era * 400;
    const qint64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const qint64 mp = (5 * doy + 2) / 153;
    const qint64 d = doy - (153 * mp + 2) / 5 + 1;
    const qint64 m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
    appendInt(s, y, 4);
    s += '-';
    appendInt(s, m, 2);
    s += '-';
    appendInt(s, d, 2);
}

void appendQuoted(std::string &s, const char *text)
{
    s += '"';
    for (const char *p = text; *p; ++p) {
        if (*p == '"') s += '"';
        s += *p;
    }
    s += '"';
}

}  // namespace

bool generateSample(const QString &path, qint64 rows, std::atomic<qint64> &done,
                    const std::atomic<bool> &cancel, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    std::string buf;
    buf.reserve(9 << 20);
    buf += "order_id,order_date,customer_id,customer_name,email,country,city,channel,product,category,"
           "quantity,unit_price,total,payment,status,note\n";

    constexpr qint64 firstDay = 19358;      // 2023-01-01
    constexpr qint64 dayCount = 1400;       // until 2026-10-31
    int countryWeights = 0;
    for (const auto &c : kCountries) countryWeights += c.weight;

    for (qint64 r = 0; r < rows; ++r) {
        Rng rng{quint64(r) * 7919 + 17};
        const Name &first = kFirst[rng.below(std::size(kFirst))];
        const Name &last = kLast[rng.below(std::size(kLast))];
        const qint64 customer = 1 + rng.below(400000);
        int pick = rng.below(countryWeights);
        const Country *country = kCountries;
        while (pick >= country->weight) pick -= (country++)->weight;
        const Product &product = kProducts[rng.below(std::size(kProducts))];
        const int qtyRoll = rng.below(100);
        const int qty = qtyRoll < 60 ? 1 : qtyRoll < 85 ? 2 : qtyRoll < 95 ? 3 : 4 + rng.below(3);
        const double unit = std::round(product.price * (0.9 + 0.2 * rng.unit()) * 100.0) / 100.0;
        const int statusRoll = rng.below(100);
        const char *status = statusRoll < 72 ? "Delivered" : statusRoll < 84 ? "Shipped"
                           : statusRoll < 91 ? "Processing" : statusRoll < 96 ? "Cancelled" : "Returned";

        buf += "ORD-";
        appendInt(buf, r + 1, 8);
        buf += ',';
        appendDate(buf, firstDay + r * dayCount / std::max<qint64>(rows, 1));   // orders come in date order
        buf += ",C";
        appendInt(buf, customer, 7);
        buf += ',';
        buf += first.text;
        buf += ' ';
        buf += last.text;
        buf += ',';
        buf += first.ascii;
        buf += '.';
        buf += last.ascii;
        appendInt(buf, customer % 1000);
        buf += "@example.com,";
        buf += country->name;
        buf += ',';
        buf += country->cities[size_t(rng.below(country->cityCount))];
        buf += ',';
        buf += kChannels[rng.below(std::size(kChannels))];
        buf += ',';
        if (std::strchr(product.name, '"')) appendQuoted(buf, product.name);
        else buf += product.name;
        buf += ',';
        buf += product.category;
        buf += ',';
        appendInt(buf, qty);
        buf += ',';
        appendMoney(buf, unit);
        buf += ',';
        appendMoney(buf, unit * qty);
        buf += ',';
        buf += kPayments[rng.below(std::size(kPayments))];
        buf += ',';
        buf += status;
        buf += ',';
        const int noteRoll = rng.below(1000);
        if (noteRoll < 60) buf += "Gift wrap";
        else if (noteRoll < 110) buf += "Express delivery";
        else if (noteRoll < 140) appendQuoted(buf, "Call before delivery, please");
        else if (noteRoll < 150) appendQuoted(buf, "Customer says \"fragile\"");
        else if (noteRoll < 152) appendQuoted(buf, "Leave at the door.\nThank you!");   // line break inside a field
        buf += '\n';

        if (buf.size() >= (8u << 20)) {
            if (file.write(buf.data(), qint64(buf.size())) != qint64(buf.size())) {
                *error = file.errorString();
                file.cancelWriting();
                return false;
            }
            buf.clear();
            done = r + 1;
            if (cancel) {
                file.cancelWriting();
                *error = QStringLiteral("Cancelled");
                return false;
            }
        }
    }
    if (file.write(buf.data(), qint64(buf.size())) != qint64(buf.size()) || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    done = rows;
    return true;
}

}  // namespace csv
