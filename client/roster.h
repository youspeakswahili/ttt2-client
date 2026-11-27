// TTT2 Client - the game's characters and stages by the game's own numbers (what TTT2_FIGHTERS / TTT2_STAGE take).
// Character numbers: the game's own (its ID_ list and name list: pau / Paul Phoenix = 0 ... tig / Tiger Jackson = 58; read
// from the game 2026-11-24 - the list before had everything from Mokujin on one off); 63 is the select grid's random cell. Names: the game's own. Stage numbers: the match settings' stage (0-17, 40-48).
#pragma once

namespace roster {

struct Character { int id; const char* name; };
// Alphabetical, as the client lists them.
inline constexpr Character kCharacters[] = {
    { 53, "Alex" }, { 39, "Alisa Bosconovitch" }, { 50, "Angel" }, { 21, "Anna Williams" }, { 30, "Armor King" },
    { 24, "Asuka Kazama" }, { 26, "Baek Doo San" }, { 34, "Bob" }, { 25, "Bruce Irvin" }, { 12, "Bryan Fury" },
    { 8, "Christie Monteiro" }, { 49, "Combot" }, { 17, "Craig Marduk" }, { 27, "Devil Jin" }, { 56, "Dr. Bosconovitch" },
    { 33, "Eddy Gordo" }, { 29, "Feng Wei" }, { 47, "Forest Law" }, { 23, "Ganryu" }, { 13, "Heihachi Mishima" },
    { 6, "Hwoarang" }, { 19, "Jack-6" }, { 9, "Jin Kazama" }, { 40, "Jinpachi Mishima" }, { 10, "Julia Chang" },
    { 42, "Jun Kazama" }, { 14, "Kazuya Mishima" }, { 3, "King" }, { 11, "Kuma" }, { 45, "Kunimitsu" },
    { 38, "Lars Alexandersson" }, { 15, "Lee Chaolan" }, { 2, "Lei Wulong" }, { 37, "Leo" }, { 31, "Lili" },
    { 7, "Ling Xiaoyu" }, { 1, "Marshall Law" }, { 51, "Michelle Chang" }, { 36, "Miguel" }, { 48, "Miharu Hirano" },
    { 18, "Mokujin" }, { 5, "Nina Williams" }, { 41, "Ogre" }, { 52, "P. Jack" }, { 43, "Panda" },
    { 0, "Paul Phoenix" }, { 28, "Raven" }, { 20, "Roger Jr." }, { 57, "Sebastian" }, { 32, "Sergei Dragunov" },
    { 46, "Slim Bob" }, { 16, "Steve Fox" }, { 58, "Tiger Jackson" }, { 54, "True Ogre" }, { 44, "Unknown" },
    { 55, "Violet" }, { 22, "Wang Jinrei" }, { 4, "Yoshimitsu" }, { 35, "Zafina" },
};
inline constexpr int kRandom = 63;

struct Stage { int id; const char* name; };
inline constexpr Stage kStages[] = {
    { 0, "Arena" }, { 1, "Festive Parade" }, { 2, "Eternal Paradise" }, { 3, "Historic Town Square" },
    { 4, "Condor Canyon" }, { 5, "Arctic Dream" }, { 6, "Dusk after the Rain" }, { 7, "Bountiful Sea" },
    { 8, "Moonlit Wilderness" }, { 9, "Wayang Kulit" }, { 10, "Fontana di Trevi" }, { 11, "Sakura Schoolyard" },
    { 12, "Tempest" }, { 13, "Winter Palace" }, { 14, "Hall of Judgement" }, { 15, "Naraku" },
    { 16, "Heavenly Garden" }, { 17, "Fallen Garden" }, { 40, "Fireworks Over Barcelona" }, { 41, "Coastline Sunset" },
    { 42, "Riverside Promenade" }, { 43, "Tropical Rainforest" }, { 44, "Moai Excavation" }, { 45, "Extravagant Underground" },
    { 46, "Tulip Festival" }, { 47, "Modern Oasis" }, { 48, "Odeum of Illusions" },
};

inline const char* CharacterName(int id)
{
    if (id == kRandom) return "Random";
    for (const Character& c : kCharacters) if (c.id == id) return c.name;
    return "Random";
}
// The game's own character ranks (0-42), by its own names.
inline constexpr const char* kRanks[] = {
    "Beginner", "9th kyu", "8th kyu", "7th kyu", "6th kyu", "5th kyu", "4th kyu", "3rd kyu", "2nd kyu", "1st kyu",
    "1st dan", "2nd dan", "3rd dan", "Disciple", "Mentor", "Master", "Grand Master",
    "Brawler", "Marauder", "Fighter", "Berserker", "Warrior", "Avenger", "Duelist", "Pugilist",
    "Vanquisher", "Destroyer", "Conqueror", "Savior", "Genbu", "Byakko", "Seiryu", "Suzaku",
    "Fujin", "Raijin", "Yaksa", "Majin", "Toshin", "Emperor", "Tekken Lord", "Tekken Emperor",
    "Tekken God", "True Tekken God",
};
inline const char* RankName(int rank)
{
    return rank >= 0 && rank < int(sizeof kRanks / sizeof kRanks[0]) ? kRanks[rank] : "?";
}
inline const char* StageName(int id)
{
    for (const Stage& s : kStages) if (s.id == id) return s.name;
    return "Random";
}

} // namespace roster
