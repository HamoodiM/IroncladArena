#include <doctest/doctest.h>

#include "TestData.h"
#include "arena/Config.h"
#include "arena/GameData.h"

using namespace arena;

TEST_CASE("Config: sections, values, comments and colour values") {
    ConfigFile file;
    std::string error;
    const char* text =
        "# full-line comment\n"
        "[enemy grunt]\n"
        "radius = 14   # trailing comment\n"
        "color = #E0604A\n"
        "\n"
        "[rules]\r\n"
        "intermission=2.5\r\n";
    REQUIRE(ParseConfig(text, file, error));
    REQUIRE(file.sections.size() == 2);
    CHECK(file.sections[0].type == "enemy");
    CHECK(file.sections[0].name == "grunt");
    CHECK(*file.sections[0].Find("radius") == "14");
    CHECK(*file.sections[0].Find("color") == "#E0604A");
    CHECK(file.sections[1].type == "rules");
    CHECK(*file.sections[1].Find("intermission") == "2.5");
}

TEST_CASE("Config: errors name the line") {
    ConfigFile file;
    std::string error;
    CHECK_FALSE(ParseConfig("[player]\nradius 16\n", file, error));
    CHECK(error.find("line 2") == 0);
    CHECK_FALSE(ParseConfig("radius = 16\n", file, error));
    CHECK(error.find("outside") != std::string::npos);
    CHECK_FALSE(ParseConfig("[player\n", file, error));
    CHECK_FALSE(ParseConfig("[player]\na = 1\na = 2\n", file, error));
    CHECK(error.find("duplicate") != std::string::npos);
}

TEST_CASE("Strict number parsing") {
    float f = 0;
    int i = 0;
    CHECK(ParseFloat("2.5", f));
    CHECK(f == 2.5f);
    CHECK(ParseFloat("+3", f));
    CHECK_FALSE(ParseFloat("2.5x", f));
    CHECK_FALSE(ParseFloat("", f));
    CHECK(ParseInt("-4", i));
    CHECK(i == -4);
    CHECK_FALSE(ParseInt("4.0", i));
}

TEST_CASE("GameData: the real data file loads and waves point at real enemies") {
    const GameData data = LoadRealGameData();
    CHECK(data.enemies.size() == 3);
    CHECK(data.FindEnemy("grunt") >= 0);
    CHECK(data.FindEnemy("brute") >= 0);
    CHECK(data.waves.size() >= 5);
    CHECK(data.player.meleeDamage[2] > data.player.meleeDamage[0]);  // the finisher hits harder
    for (const auto& w : data.waves)
        for (const auto& g : w.groups) CHECK(g.enemy < static_cast<int>(data.enemies.size()));
}

TEST_CASE("GameData: typos and bad references fail loudly") {
    GameData data;
    std::string error;
    const std::string base = "[enemy grunt]\nhp = 3\n[wave 1]\ngrunt = 2\n";
    REQUIRE(LoadGameData(base, data, error));

    CHECK_FALSE(LoadGameData("[enemy grunt]\nhpp = 3\n[wave 1]\ngrunt = 2\n", data, error));
    CHECK(error.find("unknown key 'hpp'") != std::string::npos);

    CHECK_FALSE(LoadGameData("[enemy grunt]\nhp = 3\n[wave 1]\ngrnut = 2\n", data, error));
    CHECK(error.find("no enemy called 'grnut'") != std::string::npos);

    CHECK_FALSE(LoadGameData("[enemy grunt]\nkind = wizard\n[wave 1]\ngrunt = 2\n", data, error));
    CHECK_FALSE(LoadGameData("[enemy grunt]\ncolor = red\n[wave 1]\ngrunt = 2\n", data, error));
    CHECK_FALSE(LoadGameData("[player]\nmelee_damage = 1, 2\n" + base, data, error));
    CHECK(error.find("3 comma-separated") != std::string::npos);
    CHECK_FALSE(LoadGameData("[enemy grunt]\n[wave 1]\ngrunt = 0\n", data, error));
}

TEST_CASE("GameData: a wave may come before the enemies it names") {
    GameData data;
    std::string error;
    REQUIRE(LoadGameData("[wave 1]\nbrute = 1\n[enemy brute]\nkind = brute\ncolor = #D9A23B\n", data, error));
    CHECK(data.enemies[0].kind == EnemyKind::Brute);
    CHECK(data.enemies[0].color == 0xD9A23BFFu);
}
