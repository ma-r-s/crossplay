#pragma once

// Underhand's cards: the original game's two data files, inside raw string
// literals, exactly as they come out of assets/json/ in its APK (version 1.0,
// sha1 329b9d4e0b62931dff2297d953f7f302aa5305c1) apart from line endings.
// Used with Spoopy Squad's permission (Mario, 2026-09-24).
//
// Nothing is converted: CardsReader reads these as it would read the files.
// To update, paste a file between its delimiters. host-tests/underhand checks
// them against the files themselves when UNDERHAND_DATA is set.
//
// clang-format off

namespace underhand {

inline constexpr char kGodsJson[] = R"UNDERHAND({
	"tutorial" : 0,
	"num_gods": 7,
	"previous_summon":"",
	"names" : {
		"0":"Uhl'uht'c",
		"1":"Wiindigoo",
		"2":"Rhybaax",
		"3":"Yacare",
		"4":"God of Beginnings",
		"5":"Kekujira",
		"6":"Jhai'lungr"
	},
	"progress": {
		"Uhl'uht'c":0,
		"Wiindigoo":0,
		"Rhybaax":0,
		"Yacare":0,
		"God of Beginnings":0,
		"Kekujira":0,
		"Jhai'lungr":0
	},
	"unlocked_cards":{
		"Uhl'uht'c":[22, 25],
		"Wiindigoo":[23, 97],
		"Rhybaax":[21, 70],
		"Yacare":[38, 111],
		"God of Beginnings":[96,98],
		"Kekujira":[72, 113],
		"Jhai'lungr":[112, 114]
	},
	"settings": {
		"master_volume" : 3
	}
}
)UNDERHAND";

inline constexpr char kCardsJson[] = R"UNDERHAND({
	"1": {
		"title": 						"Traveling Salesperson",
		"flavortext": 					"She probably doesn't know who she's selling to",
		"option1": {
			"optiontext": "				Buy Supplies",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				2,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist":	 			0,
				"food": 				1,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"Sell Supplies",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				2,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				1,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"Decline to Trade",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					1,
		"isrecurring": 					1,
		"animationframes": 				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"2": {
		"title": 						"Police Raid",
		"flavortext": 					"Summoning demons is only mostly illegal",
		"option1": {
			"optiontext": 				"Attempt to fight them off",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				2,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			420
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"Bribe them to leave us alone",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				3,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			420
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"There is nothing we can do",
			"outputtext": 				"You Lose",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					1
		},
		"isinitial": 					0,
		"isrecurring": 					0,
		"animationframes": 				1,
		"weight":						12,
		"cardartdone":					1
	},
	"3": {
		"title": 						"Recruiting Opportunity",
		"flavortext": 					"Fresh blood, ripe for cultivation",
		"option1": {
			"optiontext": 				"Stay Discreet",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				1,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"Hang up some posters",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				3,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			2
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"Try some kidnapping instead",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			2,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial": 					1,
		"isrecurring": 					1,
		"animationframes": 				1,
		"weight":						10,
		"cardartdone":					1
	},
	"4": {
		"title": 						"Gods Demand Sacrifice",
		"flavortext": 					"They are hungry",
		"option1": {
			"optiontext": 				"Sacrifice Prisoners",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			2,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"Sacrifice a Cultist",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				1,
				"food": 				0,
				"prisoner":	 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"We don't have anyone to spare",
			"outputtext": 				"Insert 'Wrath of the Gods'",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[5],
				"allowsdupes": 			0,
				"numcards": 			[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					1,
		"isrecurring": 					1,
		"animationframes": 				1,
		"weight":						13,
		"cardartdone":					1
	},
	"5": {
		"title": 						"Wrath of the Gods",
		"flavortext": 					"A nameless fury approaches",
		"option1": {
			"optiontext": 				"Appease them with a large sacrifice",
			"outputtext": 				"",
			"cultistequalsprisoner": 	1,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards":	 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				5,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"Appease them with a sacred offering",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				1,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"The end has come",
			"outputtext": 				"You Lose",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					1
		},
		"isinitial": 					0,
		"isrecurring": 					0,
		"animationframes": 				1,
		"weight":						15,
		"cardartdone":					1
	},
	"6": {
		"title": 						"Disease Strikes!",
		"flavortext": 					"Screams, moans and fetid smells",
		"option1": {
			"optiontext": 				"Hire a private doctor",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				3,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"Hire a doctor in town",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				2,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			1
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"Quarantine the sick and hope for the best",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				420,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					1,
		"isrecurring": 					1,
		"animationframes": 				1,
		"weight":						13,
		"cardartdone":					1
	},
	"7": {
		"title": 						"Milk Delivery",
		"flavortext": 					"He is definitely lost",
		"option1": {
			"optiontext": 				"Take the milk",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				1,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"Take him (and the milk)",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				1,
				"prisoner": 			1,
				"suspicion": 			1
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"Convert him and take the milk",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes":	 		0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				1,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				1,
				"food": 				1,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					1,
		"isrecurring": 					1,
		"animationframes": 				1,
		"weight":						10,
		"cardartdone":					1
	},
	"8": {
		"title": 						"Day Job",
		"flavortext": 					"Can't be a cultist all the time",
		"option1": {
			"optiontext": 				"Work",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				2,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"Rob the place",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				4,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			2
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"Not today",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					1,
		"isrecurring": 					1,
		"animationframes": 				1,
		"weight":						12,
		"cardartdone":					1
	},
	"9": {
		"title": 						"Wandering Children",
		"flavortext": 					"They shouldn't be out alone",
		"option1": {
			"optiontext": 				"No-one will miss two or three",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			3,
				"suspicion": 			2
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"Offer them gifts",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				1,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				3,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			2
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"Let them go",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					1,
		"isrecurring": 					1,
		"animationframes": 				1,
		"weight":						10,
		"cardartdone":					1
	},
	"10": {
		"title":	 					"Undercover Cultist",
		"flavortext": 					"Someone on the inside",
		"option1": {
			"optiontext": 				"Infiltrate the police",
			"outputtext": 				"Insert 'Reduce Suspicion'",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[11],
				"allowsdupes": 			0,
				"numcards": 			[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				1,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			1
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"Infiltrate a bank",
			"outputtext": 				"Insert 'Bank Robbery'",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[12],
				"allowsdupes": 			0,
				"numcards": 			[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				1,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			1
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"Not this time",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					1,
		"isrecurring": 					1,
		"animationframes": 				1,
		"weight":						12,
		"cardartdone":					1
	},
	"11": {
		"title": 						"Reduce Suspicion",
		"flavortext": 					"Now is our chance",
		"option1": {
			"optiontext": 				"Tamper",
			"outputtext":				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			3
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"Stay hidden",
			"outputtext": 				"Insert back into deck",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[11],
				"allowsdupes": 			0,
				"numcards": 			[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					0,
		"isrecurring": 					0,
		"animationframes": 				1,
		"weight":						10,
		"cardartdone":					1
	},
	"12": {
		"title": 						"Bank Robbery",
		"flavortext": 					"Now is our chance",
		"option1": {
			"optiontext": 				"Perform a heist",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				5,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			2
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"Stay hidden",
			"outputtext": 				"Insert back into deck",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[12],
				"allowsdupes": 			0,
				"numcards": 			[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					0,
		"isrecurring": 					0,
		"animationframes": 				1,
		"weight":						10,
		"cardartdone":					1
	},
	"13": {
		"title": 						"Bring your Child to Work Day",
		"flavortext": 					"The best way to prepare your little ones for the future",
		"option1": {
			"optiontext": 				"Indoctrinate them",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				1,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"For the greater good",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			1,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"Put them to work",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				1,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					1,
		"isrecurring": 					1,
		"animationframes": 				1,
		"weight":						10,
		"cardartdone":					1
	},
	"14": {
		"title": 						"The Necronomicon",
		"flavortext": 					"The ancient grimoire has been found",
		"option1": {
			"optiontext": 				"Learn the ways of darkness",
			"outputtext": 				"Insert x3 'Reading the Necronomicon'",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			15,
				"upperbound": 			20,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			3
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				1,
				"money": 				0,
				"cultist": 				1,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"It is too dangerous",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					1,
		"isrecurring": 					0,
		"animationframes": 				1,
		"weight":						14,
		"cardartdone":					1
	},
	"15": {
		"title": 						"Reading the Necronomicon",
		"flavortext": 					"Some things are not meant to be known",
		"option1": {
			"optiontext": 				"Melted eyes and boiled blood",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				1,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"There are none left to read",
			"outputtext": 				"(This option is only available with zero cultists)",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				840,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					0,
		"isrecurring": 					0,
		"animationframes": 				1,
		"weight":						10,
		"cardartdone":					1
	},
	"16": {
		"title": 						"Reading the Necronomicon",
		"flavortext": 					"This knowledge comes at a hideous price",
		"option1": {
			"optiontext": 				"Poverty",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				420,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					0,
		"isrecurring": 					0,
		"animationframes": 				1,
		"weight":						10,
		"cardartdone":					1
	},
	"17": {
		"title": 						"Reading the Necronomicon",
		"flavortext": 					"A blight upon those who seek the sacred knowledge",
		"option1": {
			"optiontext": 				"Decay and rot",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				420,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					0,
		"isrecurring": 					0,
		"animationframes": 				1,
		"weight":						10,
		"cardartdone":					1
	},
	"18": {
		"title": 						"Reading the Necronomicon",
		"flavortext": 					"Its aura of evil can be felt for miles",
		"option1": {
			"optiontext": 				"The wrong kind of attention",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			2
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					0,
		"isrecurring": 					0,
		"animationframes": 				1,
		"weight":						10,
		"cardartdone":					1
	},
	"19": {
		"title": 						"Reading the Necronomicon",
		"flavortext": 					"Only madness is found in these pages",
		"option1": {
			"optiontext": 				"Dead eyes and drooling mouths",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				420,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					0,
		"isrecurring": 					0,
		"animationframes": 				1,
		"weight":						10,
		"cardartdone":					1
	},
	"20": {
		"title": 						"Reading the Necronomicon",
		"flavortext": 					"Knowledge of dark rituals",
		"option1": {
			"optiontext": 				"The gods will be pleased",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				1,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					0,
		"isrecurring": 					0,
		"animationframes": 				1,
		"weight":						10,
		"cardartdone":					1
	},
	"21": {
		"title": 						"Perform Haruspicy",
		"flavortext": 					"Divine the future in animal entrails",
		"option1": {
			"optiontext": 				"Make a sacrifice",
			"outputtext": 				"Foresight",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		1,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				1,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"We need the food",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					0,
		"isrecurring": 					1,
		"animationframes": 				1,
		"weight":						12,
		"cardartdone":					1
	},
	"22": {
		"title": 						"Soothsayer in town!",
		"flavortext": 					"Knowledge is power",
		"option1": {
			"optiontext": 				"See the future",
			"outputtext": 				"Foresight",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		1,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option2": {
			"optiontext": 				"Change the future",
			"outputtext": 				"Foresight with discard",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		1,
				"candiscard": 			1
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				1,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"option3": {
			"optiontext": 				"The future should be a mystery",
			"outputtext": 				"",
			"cultistequalsprisoner": 	0,
			"foresight": {
				"hasforesight": 		0,
				"candiscard": 			0
			},
			"shuffle": {
				"lowerbound": 			999,
				"upperbound": 			999,
				"specificids": 			[],
				"allowsdupes": 			0,
				"numcards": 			0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"rewards": {
				"relic": 				0,
				"money": 				0,
				"cultist": 				0,
				"food": 				0,
				"prisoner": 			0,
				"suspicion": 			0
			},
			"iswin":					"",
			"islose": 					0
		},
		"isinitial": 					0,
		"isrecurring": 					1,
		"animationframes": 				1,
		"weight":						13,
		"cardartdone":					1
	},
	"23": {
		"title":						"Organ Harvest",
		"flavortext":					"The most important organ is the organization",
		"option1": {
			"optiontext":				"We need the cash",
			"outputtext": 				"",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				2,
				"cultist":				0,
				"prisoner":				1,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"We've got the meats",
			"outputtext": 				"",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				1,
				"food":					2,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Save the slaughter for another day",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					1,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"24": {
		"title":						"Tax Day",
		"flavortext":					"The tax man cometh",
		"option1": {
			"optiontext":				"Pay",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				2,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"We can't afford it right now",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			2
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Kidnap the tax collector",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				1,
				"food":					0,
				"suspicion":			3
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					1,
		"isrecurring":					1,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"25": {
		"title":						"Sacrificial Pact",
		"flavortext":					"Great power at great price",
		"option1": {
			"optiontext":				"Material Wealth",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				5,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Souls for the work",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				5,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Our relics are too valuable",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					1,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"26": {
		"title":						"Potion Vendor",
		"flavortext":					"We can't handle his strongest potions",
		"option1": {
			"optiontext":				"Buy a Draught of Vitality",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				1,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Buy a Tincture of Duplication",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				1,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"We can't handle his weakest potions either",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					1,
		"isrecurring":					1,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"27": {
		"title":						"Desperate Measures",
		"flavortext":					"Our food supplies are dangerously low",
		"option1": {
			"optiontext":				"We can make do with a captive",
			"outputtext": 				"Insert 'Red Dreams'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[63],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				1,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					3,
				"suspicion":			2
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Draw lots",
			"outputtext": 				"Insert 'Red Dreams'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[63],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					3,
				"suspicion":			1
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Even cultists have limits",
			"outputtext": 				"You Lose",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					1
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"28": {
		"title":						"Tax Break",
		"flavortext":					"We made sure to register as a religious non-profit",
		"option1": {
			"optiontext":				"Invest the money",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				2,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Spend some, save some",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				1,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Time for a shopping trip",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					1,
		"isrecurring":					1,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"29": {
		"title":						"Rumors of Darkness",
		"flavortext":					"Whispers of ancient ruins in the jungle",
		"option1": {
			"optiontext":				"Prepare an expedition",
			"outputtext": 				"Insert 'Expedition'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[30],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Another Time",
			"outputtext": 				"Insert back into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[29],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"The rumors are baseless",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						100,
		"cardartdone":					1
	},
	"30": {
		"title":						"An Expedition",
		"flavortext":					"Fortune and glory",
		"option1": {
			"optiontext":				"Send a large party",
			"outputtext": 				"Insert the next Expedition event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[31],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Send a small party",
			"outputtext": 				"Insert the next Expedition event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[32],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Maybe another time",
			"outputtext": 				"Insert 'Expedition'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[30],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						100,
		"cardartdone":					1
	},
	"31": {
		"title":						"The Haunted Jungle",
		"flavortext":					"In the canopy-darkness, the road forks",
		"option1": {
			"optiontext":				"Turn Left",
			"outputtext": 				"Insert the next Expedition event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[33],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Turn Right",
			"outputtext": 				"Insert the next Expedition event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[31],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Turn Back",
			"outputtext": 				"Insert 'Expedition'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[30],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						100,
		"cardartdone":					1
	},
	"32": {
		"title":						"The Haunted Jungle",
		"flavortext":					"In the canopy-darkness, the road forks",
		"option1": {
			"optiontext":				"Turn Left",
			"outputtext": 				"Insert the next Expedition event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[34],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Turn Right",
			"outputtext": 				"Insert the next Expedition event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[34],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Turn Back",
			"outputtext": 				"Insert 'Expedition'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[30],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						100,
		"cardartdone":					1
	},
	"33": {
		"title":						"Cyclopean Ruins",
		"flavortext":					"Nameless horrors stalk the corridors",
		"option1": {
			"optiontext":				"Attempt to sneak by",
			"outputtext": 				"Insert the next Expedition event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[36],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Attempt to fight them off",
			"outputtext": 				"Insert the next Expedition event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[35],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						100,
		"cardartdone":					1
	},
	"34": {
		"title":						"Dead End",
		"flavortext":					"Lost and Alone",
		"option1": {
			"optiontext":				"The end is nigh",
			"outputtext": 				"Insert 'Expedition'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[30],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Send a rescue party",
			"outputtext": 				"Insert the next Expedition event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[33],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						100,
		"cardartdone":					1
	},
	"35": {
		"title":						"The Aftermath",
		"flavortext":					"The demons lay dead, and a great beast lays before us",
		"option1": {
			"optiontext":				"Awaken it",
			"outputtext": 				"Summon Rhybaax",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"Rhybaax",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Leave it be",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Linger a little longer",
			"outputtext": 				"Insert this event back into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[35],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						100,
		"cardartdone":					1
	},
	"36": {
		"title":						"The Aftermath",
		"flavortext":					"We could not outrun them",
		"option1": {
			"optiontext":				"If at first you don't succeed...",
			"outputtext": 				"Insert 'Expedition'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[30],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Send reinforcements",
			"outputtext": 				"Insert the next Expedition event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[35],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						100,
		"cardartdone":					1
	},
	"37": {
		"title":						"Auction on Relic",
		"flavortext":					"They know not what they are selling",
		"option1": {
			"optiontext":				"We must have it",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				5,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Arrange to have it stolen",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			3
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Pass on it, reluctantly",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					1,
		"isrecurring":					1,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"38": {
		"title":						"Start a farm",
		"flavortext":					"We should learn to provide for ourselves",
		"option1": {
			"optiontext":				"Sow the earth",
			"outputtext": 				"Insert x3 'Harvest'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			39,
				"upperbound":			44,
				"specificids":			[],
				"allowsdupes":			1,
				"numcards":				3
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				2,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Maybe another time",
			"outputtext": 				"Insert back into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[38],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"This sounds too difficult",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"39": {
		"title":						"Bountiful Harvest",
		"flavortext":					"We have been blessed",
		"option1": {
			"optiontext":				"We could use food",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					3,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Resow the crop",
			"outputtext": 				"Insert x2 'Harvest'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			39,
				"upperbound":			44,
				"specificids":			[],
				"allowsdupes":			1,
				"numcards":				2
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Sell the food in town",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				3,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"40": {
		"title":						"Harvest",
		"flavortext":					"Autumn comes again",
		"option1": {
			"optiontext":				"We could use food",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Resow the crop",
			"outputtext": 				"Insert 'Harvest'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			39,
				"upperbound":			44,
				"specificids":			[],
				"allowsdupes":			1,
				"numcards":				1
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Sell the food in town",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				1,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"41": {
		"title":						"Crop Failure",
		"flavortext":					"The earth witholds its bounty",
		"option1": {
			"optiontext":				"Curses!",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"42": {
		"title":						"Dark Harvest",
		"flavortext":					"What hideous crops are these? ",
		"option1": {
			"optiontext":				"This shall be a suitable offering",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Resow the magic plant",
			"outputtext": 				"Insert x4 'Harvest'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			39,
				"upperbound":			44,
				"specificids":			[],
				"allowsdupes":			1,
				"numcards":				4
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Sell the plant in town",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				5,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			1
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"43": {
		"title":						"Crop Failure",
		"flavortext":					"The earth witholds its bounty",
		"option1": {
			"optiontext":				"Curses!",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"44": {
		"title":						"Harvest",
		"flavortext":					"Autumn comes again",
		"option1": {
			"optiontext":				"We could use food",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Resow the crop",
			"outputtext": 				"Insert 'Harvest'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			39,
				"upperbound":			44,
				"specificids":			[],
				"allowsdupes":			1,
				"numcards":				1
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Sell the food in town",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				1,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"45": {
		"title":						"A Rival Cult",
		"flavortext":					"There are others who worship our dark gods",
		"option1": {
			"optiontext":				"Raid their compound",
			"outputtext": 				"Insert x2 'Spoils of War'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			46,
				"upperbound":			50,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				2
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Trade will strengthen us both",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				2,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Leave them be",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					1,
		"isrecurring":					1,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"46": {
		"title":						"Spoils of War",
		"flavortext":					"Only death and desolation",
		"option1": {
			"optiontext":				"Foiled again",
			"outputtext": 				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"47": {
		"title":						"Spoils of War",
		"flavortext":					"A holy relic",
		"option1": {
			"optiontext":				"We shall add this to our collection",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Blasphemy",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"48": {
		"title":						"Spoils of War",
		"flavortext":					"A few survivors",
		"option1": {
			"optiontext":				"They seem suitable for sacrifice",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				3,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Slaughter them",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"49": {
		"title":						"Spoils of War",
		"flavortext":					"Ill-gotten riches",
		"option1": {
			"optiontext":				"Refill our coffers",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				2,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"We don't need it",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"50": {
		"title":						"Spoils of War",
		"flavortext":					"All manner of foodstuffs",
		"option1": {
			"optiontext":				"We shall feast tonight",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Our stores are full",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"51": {
		"title":						"Ancestral Reckoning",
		"flavortext":					"The actions of your ancestors are inescapable",
		"option1": {
			"optiontext":				"No one can avoid fate",
			"outputtext": 				"Insert x2 'Ancestor'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			52,
				"upperbound":			55,
				"specificids":			[],
				"allowsdupes":			1,
				"numcards":				2
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					1,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"52": {
		"title":						"Ancestor",
		"flavortext":					"A devout follower of the gods",
		"option1": {
			"optiontext":				"The gods reward you",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Prayers for bounty are answered",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				2,
				"cultist":				0,
				"prisoner":				0,
				"food":					3,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"We need no reward",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"53": {
		"title":						"Ancestor",
		"flavortext":					"Destroyer of our attempts at greatness",
		"option1": {
			"optiontext":				"Suffer the wrath",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Offer a sacrifice",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				3,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"The furies shall approach again",
			"outputtext": 				"Insert 'Wrath of the Gods'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[5],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"54": {
		"title":						"Ancestor",
		"flavortext":					"Convert away from the great faith",
		"option1": {
			"optiontext":				"Followers discover your hypocrisy",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				420,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"None may leave",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				3,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				2,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"55": {
		"title":						"Ancestor",
		"flavortext":					"Small-time cultist",
		"option1": {
			"optiontext":				"Inspiring future generations",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"A small reward from the gods",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Gather the leftover inheritance",
			"outputtext": 				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				1,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight":						10,
		"cardartdone":					1
	},
	"56": {
		"title":						"Greed",
		"flavortext":					"The deadliest of sins",
		"option1": {
			"optiontext":				"Punished accordingly",
			"outputtext":				"Discard 5 cards at random",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		5,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"A suitable apology",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				2,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"57": {
		"title":						"The Call of Uhl'uht'c",
		"flavortext":					"It resounds in your soul",
		"option1": {
			"optiontext":				"Answer",
			"outputtext":				"Insert 'Uhl'uht'c's 1st Request'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[58],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Remain Silent",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"58": {
		"title":						"Uhl'uht'c's First Request",
		"flavortext":					"Only you can stir it from its slumber",
		"option1": {
			"optiontext":				"Speak its name",
			"outputtext":				"Insert 'Uhl'uht'c's 2nd Request'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[59],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			3
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Abandon your task",
			"outputtext":				"Insert 'Wrath of the Gods'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[5],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"59": {
		"title":						"Uhl'uht'c's Second Request",
		"flavortext":					"Souls to feed its power",
		"option1": {
			"optiontext":				"Make a sacrifice",
			"outputtext":				"Insert 'Uhl'uht'c's 3rd Request'",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[60],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				3,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Abandon your task",
			"outputtext":				"Insert 2x 'Wrath of the Gods'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[5],
				"allowsdupes":			0,
				"numcards":				[2]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"60": {
		"title":						"Uhl'uht'c's Third Request",
		"flavortext":					"A body of gold and iron",
		"option1": {
			"optiontext":				"Prepare a host",
			"outputtext":				"Insert 'Uhl'uht'c's 4th Request'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[61],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				3,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Abandon your task",
			"outputtext":				"Insert 3x 'Wrath of the Gods'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[5],
				"allowsdupes":			0,
				"numcards":				[3]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"61": {
		"title":						"Uhl'uht'c's Fourth Request",
		"flavortext":					"Its presence is almost palpable",
		"option1": {
			"optiontext":				"Paint a circle in blood",
			"outputtext":				"Insert 'Uhl'uht'c'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[62],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				2,
				"cultist":				2,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Abandon your task",
			"outputtext":				"Insert 4x 'Wrath of the Gods'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[5],
				"allowsdupes":			0,
				"numcards":				[4]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"62": {
		"title":						"Uhl'uht'c",
		"flavortext":					"Your mind burns with its final demand",
		"option1": {
			"optiontext":				"The ritual",
			"outputtext":				"Summon Uhl'uht'c",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"Uhl'uht'c",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Abandon your task",
			"outputtext":				"Insert 5x 'Wrath of the Gods'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[5],
				"allowsdupes":			0,
				"numcards":				[5]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"63": {
		"title":						"Red Dreams",
		"flavortext":					"You just can't forget the taste",
		"option1": {
			"optiontext":				"Indulge again",
			"outputtext":				"Insert the next Peculiar Palate event into deck",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[64],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Perhaps just once more",
			"outputtext":				"",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Suppress your desire for now",
			"outputtext":				"Insert 'Red Dreams'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[63],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"64": {
		"title":						"A Hungry Guest",
		"flavortext":					"He seems to share your dietary preferences",
		"option1": {
			"optiontext":				"Invite him to dine with you",
			"outputtext":				"Insert the next Peculiar Palate event into deck",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[65],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				1,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"It seems only right that you should eat him too",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"65": {
		"title":						"A Proposition",
		"flavortext":					"Your men, for his god",
		"option1": {
			"optiontext":				"You're happy to oblige",
			"outputtext":				"Insert the next Peculiar Palate event into deck",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[66],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"You'd rather eat them yourself",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"66": {
		"title":						"The Wiindigoo",
		"flavortext":					"Hunger for flesh, made flesh",
		"option1": {
			"optiontext":				"Welcome it into the world",
			"outputtext":				"Summon Wiindigoo",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				1,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"Wiindigoo",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Second thoughts",
			"outputtext":				"Insert 'Wrath of the Gods'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[5],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"67": {
		"title":						"A Charitable Donation",
		"flavortext":					"Perhaps a way to deflect suspicion",
		"option1": {
			"optiontext":				"Food for the hungry",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			1
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Money for the poor",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				1,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			1
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Charity is for the weak",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					1,
		"isrecurring":					1,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"68": {
		"title":						"Infestation",
		"flavortext":					"Maggots in the pantry",
		"option1": {
			"optiontext":				"Salvage what remains",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					420,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Hire an exterminator",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				2,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Infested or not, it's still food",
			"outputtext":				"",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				3,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					1,
		"isrecurring":					1,
		"animationframes":				1,
		"weight":						13,
		"cardartdone":					1
	},
	"69": {
		"title":						"The Passage of Time",
		"flavortext":					"Time heals all wounds",
		"option1": {
			"optiontext":				"Suspicion has begun to wear off",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			1
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Interest pays dividends",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				1,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"We've kept busy in other ways",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					1,
		"isrecurring":					1,
		"animationframes":				1,
		"weight":						12,
		"cardartdone":					1
	},
	"70": {
		"title":						"Necromancy",
		"flavortext":					"The dead make willing servants",
		"option1": {
			"optiontext":				"We have a couple of extra prisoners",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				2,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Some townsfolk will do the trick",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			1
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"We prefer living servants",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					1,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"71": {
		"title":						"This is an Event Card",
		"flavortext":					"Each event card has up to three options",
		"option1": {
			"optiontext":				"Select this option to proceed",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[91],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"72": {
		"title":						"Aeromancy",
		"flavortext":					"Being devout has its advantages",
		"option1": {
			"optiontext":				"Pray for rain",
			"outputtext":				"Insert 'Rain'",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[73],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Pray for sun",
			"outputtext":				"Insert 'Golden Sun'",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[76],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"This weather is acceptable",
			"outputtext":				"Insert 'Aeromancy'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[72],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"73": {
		"title":						"Rain",
		"flavortext":					"A great downpour",
		"option1": {
			"optiontext":				"Ideal for covert activites",
			"outputtext":				"Insert 'Storm'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[74],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			420
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"This is enough for now",
			"outputtext":				"Insert 'Aeromancy'",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[72],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			420
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"74": {
		"title":						"Storm",
		"flavortext":					"The flood waters rise",
		"option1": {
			"optiontext":				"It sweeps away the evidence of our sins",
			"outputtext":				"Insert 'Hurricane'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[75],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			420
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Maybe a little less rain",
			"outputtext":				"Insert 'Rain'",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[73],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			420
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"This has to stop now",
			"outputtext":				"Insert 'Aeromancy'",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[72],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			420
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"75": {
		"title":						"Hurricane",
		"flavortext":					"Nature's fury unleashed",
		"option1": {
			"optiontext":				"Devastation",
			"outputtext":				"You Lose",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					1
		},
		"option2": {
			"optiontext":				"A desperate sacrifice",
			"outputtext":				"Insert 'Aeromancy'",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[72],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				4,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"A holy sacrifice",
			"outputtext":				"Insert 'Aeromancy'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[72],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"76": {
		"title":						"Golden Sun",
		"flavortext":					"Such wonderful warmth",
		"option1": {
			"optiontext":				"This will be good for our crops",
			"outputtext":				"Insert 'A Long Summer'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[77],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"This is enough for now",
			"outputtext":				"Insert 'Aeromancy'",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[72],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"77": {
		"title":						"A Long Summer",
		"flavortext":					"Searing heat",
		"option1": {
			"optiontext":				"A marquee year for farming",
			"outputtext":				"Insert 'Drought'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[78],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Maybe a little less heat",
			"outputtext":				"Insert 'Golden Sun'",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[76],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"This has to stop now",
			"outputtext":				"Insert 'Aeromancy'",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[72],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"78": {
		"title":						"Drought",
		"flavortext":					"Merciless sun",
		"option1": {
			"optiontext":				"Dessication",
			"outputtext":				"You Lose",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					1
		},
		"option2": {
			"optiontext":				"A desperate sacrifice",
			"outputtext":				"Insert 'Aeromancy'",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[72],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				4,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"A holy sacrifice",
			"outputtext":				"Insert 'Aeromancy'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[72],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"79": {
		"title":						"A Mysterious Egg",
		"flavortext":					"An oblong curio in a local shop",
		"option1": {
			"optiontext":				"Purchase it",
			"outputtext":				"Insert the next Eldritch Beast event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[80],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				3,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Steal it",
			"outputtext":				"Insert the next Eldritch Beast event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[80],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			2
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Leave it",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"80": {
		"title":						"Egg Hatching",
		"flavortext":					"Something emerges from within",
		"option1": {
			"optiontext":				"Assign a cultist to care for the hatchling",
			"outputtext":				"Insert the next Eldritch Beast event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[81],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"This will make an interesting omelette",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"81": {
		"title":						"Horrific Hatchling",
		"flavortext":					"It requires sustenance",
		"option1": {
			"optiontext":				"Feed it from our stores",
			"outputtext":				"Insert the next Eldritch Beast event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[82],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Feed it a prisoner",
			"outputtext":				"Insert the next Eldritch Beast event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[82],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				1,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Kill it",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"82": {
		"title":						"Carnivorous Caiman",
		"flavortext":					"It grows insatiable",
		"option1": {
			"optiontext":				"Slaughter a couple of cows",
			"outputtext":				"Insert the next Eldritch Beast event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[83],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"More prisoners should do the trick",
			"outputtext":				"Insert the next Eldritch Beast event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[83],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				2,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Kill it",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"83": {
		"title":						"Rampaging Reptile",
		"flavortext":					"The beast is ravenous",
		"option1": {
			"optiontext":				"Let it loose in the pantry",
			"outputtext":				"Insert the next Eldritch Beast event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[84],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					4,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Unleash it on the prisoners",
			"outputtext":				"Insert the next Eldritch Beast event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[84],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				4,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"We cannot sate its hunger",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					1
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"84": {
		"title":						"Yacare, Fury of the Wild",
		"flavortext":					"We have awakened its true power",
		"option1": {
			"optiontext":				"A suitable sacrifice is required",
			"outputtext":				"Summon Yacare",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"Yacare",
			"islose":					0
		},
		"option2": {
			"optiontext":				"This was not our intention",
			"outputtext":				"Insert 2x 'Wrath of the Gods'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[5],
				"allowsdupes":			0,
				"numcards":				[2]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"85": {
		"title":						"A Fishing Trip",
		"flavortext":					"Reap the bounty of the sea",
		"option1": {
			"optiontext":				"Stay close to shore",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Venture further out",
			"outputtext":				"Insert 'Catch of the Day'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			86,
				"upperbound":			89,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				1
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Let the sea keep its bounty",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"86": {
		"title":						"Catch of the Day",
		"flavortext":					"A frighteningly large fish",
		"option1": {
			"optiontext":				"This will make a fine meal",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Cast it back into the sea",
			"outputtext":				"Insert 'Catch of the Day'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			86,
				"upperbound":			89,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				1
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"87": {
		"title":						"Catch of the Day",
		"flavortext":					"Nothing is biting",
		"option1": {
			"optiontext":				"Rats!",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Use more bait",
			"outputtext":				"Insert 'Catch of the Day'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			86,
				"upperbound":			89,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				1
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"88": {
		"title":						"Catch of the Day",
		"flavortext":					"Sunken Treasure",
		"option1": {
			"optiontext":				"Ancient coins and jewelry",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				2,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Return it to the depths",
			"outputtext":				"Insert 'Catch of the Day'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			86,
				"upperbound":			89,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				1
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"89": {
		"title":						"Catch of the Day",
		"flavortext":					"A powerful tug on the line",
		"option1": {
			"optiontext":				"All hands on deck!",
			"outputtext":				"Insert 'Kekujira the Cursed Whale'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[90],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				3,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Let this one get away",
			"outputtext":				"Insert 'Catch of the Day'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			86,
				"upperbound":			89,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				1
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"90": {
		"title":						"Kekujira the Cursed Whale",
		"flavortext":					"An angry leviathan arises",
		"option1": {
			"optiontext":				"Calm it with holy relics",
			"outputtext":				"Summon Kekujira",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				2,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"Kekujira",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Appease it with a sacrifice",
			"outputtext":				"Summon Kekujira",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				5,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"Kekujira",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Flee before the fury of the deep",
			"outputtext":				"Insert 2x 'Wrath of the Gods'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[5],
				"allowsdupes":			0,
				"numcards":				[2]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"91": {
		"title":						"Some Options Provide Resources",
		"flavortext":					"Rewards and effects are shown at the bottom of the option box",
		"option1": {
			"optiontext":				"This option gives you one of each resource",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[92],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				1,
				"money":				1,
				"cultist":				1,
				"prisoner":				1,
				"food":					1,
				"suspicion":			1
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"92": {
		"title":						"Other Options Require Resources",
		"flavortext":					"Drag these resources from your hand",
		"option1": {
			"optiontext":				"The middle of the option box shows these requirements",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[93],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				1,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			1
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"93": {
		"title":						"Adding Cards to the Deck",
		"flavortext":					"Some options add cards to your event deck, starting event chains",
		"option1": {
			"optiontext":				"This is denoted by the 'Insert' keyword",
			"outputtext":				"Insert 'Tips and Tricks'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[94],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				1,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"94": {
		"title":						"God Event Chains",
		"flavortext":					"Some event chains will culminate with a god",
		"option1": {
			"optiontext":				"Summon a god to win the game",
			"outputtext":				"Insert 'Progression'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[95],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"95": {
		"title":						"Relic Usage",
		"flavortext":					"Relics can act as wild cards",
		"option1": {
			"optiontext":				"A relic can be used to replace one of the food resources required here",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[99],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				1,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"96": {
		"title":						"Beginner's Luck",
		"flavortext":					"The first time's always the easiest",
		"option1": {
			"optiontext":				"We could use some cultists",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"We could use some food",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"We could use some money",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				2,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"97": {
		"title":						"Tasting the Future",
		"flavortext":					"Visions of tomorrow, imprinted on flesh",
		"option1": {
			"optiontext":				"Prepare the table",
			"outputtext":				"Foresight",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			1,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Kidnap some townsfolk",
			"outputtext":				"Foresight",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			1,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			1
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"We are content with the present",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					1,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"98": {
		"title":						"Inheritance",
		"flavortext":					"Even the old can serve the cause",
		"option1": {
			"optiontext":				"Liquify the assets",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				3,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Keep the assets",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"99": {
		"title":						"Prisoners and Cultists",
		"flavortext":					"Sometimes cultists and prisoners can be used interchangeably",
		"option1": {
			"optiontext":				"This symbol means a mix of prisoners and cultists can be used",
			"outputtext":				"",
			"cultistequalsprisoner":	1,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[100],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"100": {
		"title":						"Tips and Tricks",
		"flavortext":					"Relics are valuable, and suspicion is dangerous",
		"option1": {
			"optiontext":				"Try to collect relics; they are often used to summon gods",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[102],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"Avoid suspicion, or face the anger of the police",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[101],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			1
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"101": {
		"title":						"A Mistake",
		"flavortext":					"We told you suspicion was bad...",
		"option1": {
			"optiontext":				"Next time we shall not be so lenient",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[102],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			1
			},
			"rewards": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"102": {
		"title":						"Progression",
		"flavortext":					"Not all options in an event chain will make progress",
		"option1": {
			"optiontext":				"This option will make progress, but is expensive",
			"outputtext":				"Insert 'The God of Beginnings'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[104],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				3,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				1,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"This option will maintain your current position",
			"outputtext":				"Insert this card back into your deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[103],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"103": {
		"title":						"Strategy",
		"flavortext":					"Try to anticipate what resources you will need",
		"option1": {
			"optiontext":				"Collect resources to proceed towards victory",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[102],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				1,
				"money":				1,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"104": {
		"title":						"Losing",
		"flavortext":					"Some options will cause you to lose if you choose them",
		"option1": {
			"optiontext":				"You can only choose losing options if you have no other choice",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[105],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				1,
				"money":				1,
				"cultist":				1,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option2": {
			"optiontext":				"This option is locked",
			"outputtext":				"You Lose",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					1
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"105": {
		"title":						"The God of Beginnings",
		"flavortext":					"Options that summon a god cause you to win",
		"option1": {
			"optiontext":				"Choose this option to complete your initiation",
			"outputtext":				"Summon 'The God of Beginnings'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				1,
				"money":				1,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"God of Beginnings",
			"islose":					0
		},
		"option2": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"106": {
		"title":						"Start a Tea Shop",
		"flavortext":					"We'll call it The Matcha Faction",
		"option1": {
			"optiontext":				"Open a small storefront",
			"outputtext":				"Insert the next Prosperity event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[108],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				1,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			1
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"It's not in our cards right now",
			"outputtext":				"Insert 'Start a Tea Shop'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[106],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"It's too much hassle",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"107": {
		"title":						"Failing Business",
		"flavortext":					"We failed at this critical steep",
		"option1": {
			"optiontext":				"If at first you don't succeed",
			"outputtext":				"Insert the next Prosperity event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			107,
				"upperbound":			108,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				1
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				1,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			1
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"It just needs a spark",
			"outputtext":				"Insert the next Prosperity event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[108],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				2,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"It's not in our cards right now",
			"outputtext":				"Insert 'Start a Tea Shop'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[106],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"108": {
		"title":						"Small Business",
		"flavortext":					"Frequented by the locals",
		"option1": {
			"optiontext":				"Get out while we're ahead",
			"outputtext":				"Insert 'Start a Tea Shop'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[106],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				3,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Reinvest our fortune",
			"outputtext":				"Insert the next Prosperity event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			108,
				"upperbound":			109,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				1
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				2,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			2
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Merely sustain it",
			"outputtext":				"Insert the next Prosperity event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			107,
				"upperbound":			108,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				1
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				1,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			1
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"109": {
		"title":						"Booming Business",
		"flavortext":					"Established and respected",
		"option1": {
			"optiontext":				"Get out while we're ahead",
			"outputtext":				"Insert 'Start a Tea Shop'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[106],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				4,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Reinvest our fortune",
			"outputtext":				"Insert the next Prosperity event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			109,
				"upperbound":			110,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				1
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				2,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			3
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Merely sustain it",
			"outputtext":				"Insert the next Prosperity event into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			108,
				"upperbound":			109,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				1
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				1,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			1
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"110": {
		"title":						"Jhai'Ti",
		"flavortext":					"The Matcha Faction has caught its attention",
		"option1": {
			"optiontext":				"Retire with its blessing",
			"outputtext":				"Insert 'Start a Tea Shop'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[106],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Offer it our finest brew in a sacred vessel",
			"outputtext":				"Summon Jhai'Ti",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"Jhai'lungr",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Spit in his cup",
			"outputtext":				"Insert 2x 'Wrath of the Gods'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[5],
				"allowsdupes":			0,
				"numcards":				[2]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						50,
		"cardartdone":					1
	},
	"111": {
		"title":						"Oomancy",
		"flavortext":					"An eggcellent way to see the future",
		"option1": {
			"optiontext":				"Break an egg",
			"outputtext":				"Foresight",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			1,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Break a few eggs",
			"outputtext":				"Foresight with Discard",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			1,
				"candiscard":			1
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"This must be some kind of yolk",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					1,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"112": {
		"title":						"Assassination",
		"flavortext":					"We need merely give the word",
		"option1": {
			"optiontext":				"Target a rich investor",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				3,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Target a key witness",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					0,
				"suspicion":			3
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"We shall be merciful for now",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					1,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"113": {
		"title":						"Holiday",
		"flavortext":					"A sacred day of festivities",
		"option1": {
			"optiontext":				"Celebrate quietly",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				1,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				1,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Celebrate loudly",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				2,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"We have more important things to do",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					1,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"114": {
		"title":						"Importing Tea",
		"flavortext":					"Mysterious teas from far away lands",
		"option1": {
			"optiontext":				"Buy a shipment",
			"outputtext":				"Insert 'Tea'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			115,
				"upperbound":			118,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				1
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				1,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Buy a few shipments",
			"outputtext":				"Insert 2x 'Tea'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			115,
				"upperbound":			118,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				2
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				2,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"Perhaps another time",
			"outputtext":				"Reshuffle into deck",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[114],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"115": {
		"title":						"Tea",
		"flavortext":					"A shipment of high quality tea",
		"option1": {
			"optiontext":				"We shall drink it ourselves",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					3,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Sell it",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				3,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"116": {
		"title":						"Tea",
		"flavortext":					"Perfect consistency for fortune telling",
		"option1": {
			"optiontext":				"Read the future",
			"outputtext":				"Foresight",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			1,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Drink it instead",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					1,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"117": {
		"title":						"Tea",
		"flavortext":					"A hypnotizing brew",
		"option1": {
			"optiontext":				"Show some prisoners the way",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				2,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Some townsfolk might like a taste",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				0
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				2,
				"prisoner":				0,
				"food":					0,
				"suspicion":			1
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"We need not resort to such methods",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	},
	"118": {
		"title":						"Tea",
		"flavortext":					"A forbidden recipe long forgotten",
		"option1": {
			"optiontext":				"Best not to drink this one",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				1,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
			},
		"option2": {
			"optiontext":				"Drink it anyways",
			"outputtext":				"Insert 'Wrath of the Gods'",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[5],
				"allowsdupes":			0,
				"numcards":				[1]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				1,
				"money":				2,
				"cultist":				0,
				"prisoner":				0,
				"food":					2,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"option3": {
			"optiontext":				"",
			"outputtext":				"",
			"cultistequalsprisoner":	0,
			"foresight": {
				"hasforesight":			0,
				"candiscard":			0
			},
			"shuffle": {
				"lowerbound":			999,
				"upperbound":			999,
				"specificids":			[],
				"allowsdupes":			0,
				"numcards":				[]
			},
			"randomrequirements":		0,
			"requirements": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"rewards": {
				"relic":				0,
				"money":				0,
				"cultist":				0,
				"prisoner":				0,
				"food":					0,
				"suspicion":			0
			},
			"iswin":					"",
			"islose":					0
		},
		"isinitial":					0,
		"isrecurring":					0,
		"animationframes":				1,
		"weight": 						10,
		"cardartdone":					1
	}
}
)UNDERHAND";

}  // namespace underhand

// clang-format on
