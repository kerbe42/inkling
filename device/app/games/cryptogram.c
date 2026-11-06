/* Cryptogram: a famous quote or proverb in which every letter has been swapped for another (the same swap all
 * through, and no letter stands for itself). Each letter of the quote is a box with its code letter printed small
 * below it. Tap a box, then a letter on the keypad: that guess fills every box with the same code letter (Erase
 * clears it; a letter already used elsewhere moves). Easy gives away 3-4 letters, Medium 1-2, Hard none. Taps only.
 * Half the games use one of about 140 quotes and 300 proverbs (none repeats until all have been played, across
 * restarts), the other half a sentence made up from a small grammar and word lists (billions of them: "Seven hungry
 * owls ate forty plums at midnight."). */
#include "../games.h"
#include "bag.h"
#include <stdio.h>
#include <string.h>

#define M 20
#define MAXQ 120

typedef struct { const char *text, *author; } Quote;

/* Quotes whose wording and author are well documented, all long out of copyright (translations: old public-domain
 * ones). Sayings that are only popularly attributed to someone are among the proverbs below without a name, or left
 * out. */
static const Quote QUOTES[] = {
	{ "The journey of a thousand miles begins with a single step.", "Lao Tzu" },
	{ "Well done is better than well said.", "Benjamin Franklin" },
	{ "Early to bed and early to rise makes a man healthy, wealthy, and wise.", "Benjamin Franklin" },
	{ "Lost time is never found again.", "Benjamin Franklin" },
	{ "Little strokes fell great oaks.", "Benjamin Franklin" },
	{ "One today is worth two tomorrows.", "Benjamin Franklin" },
	{ "Whatever is begun in anger ends in shame.", "Benjamin Franklin" },
	{ "Hunger never saw bad bread.", "Benjamin Franklin" },
	{ "Diligence is the mother of good luck.", "Benjamin Franklin" },
	{ "He that lies down with dogs shall rise up with fleas.", "Benjamin Franklin" },
	{ "Glass, china, and reputation are easily cracked, and never well mended.", "Benjamin Franklin" },
	{ "Fish and visitors stink in three days.", "Benjamin Franklin" },
	{ "There are no gains without pains.", "Benjamin Franklin" },
	{ "Eat to live, and not live to eat.", "Benjamin Franklin" },
	{ "Love your neighbour; yet don't pull down your hedge.", "Benjamin Franklin" },
	{ "He that can have patience can have what he will.", "Benjamin Franklin" },
	{ "Drive thy business; let not that drive thee.", "Benjamin Franklin" },
	{ "An ounce of prevention is worth a pound of cure.", "Benjamin Franklin" },
	{ "The used key is always bright.", "Benjamin Franklin" },
	{ "Wish not so much to live long as to live well.", "Benjamin Franklin" },
	{ "He that falls in love with himself will have no rivals.", "Benjamin Franklin" },
	{ "Be slow in choosing a friend, slower in changing.", "Benjamin Franklin" },
	{ "A good example is the best sermon.", "Benjamin Franklin" },
	{ "Plough deep while sluggards sleep.", "Benjamin Franklin" },
	{ "Never leave that till tomorrow which you can do today.", "Benjamin Franklin" },
	{ "Remember that time is money.", "Benjamin Franklin" },
	{ "Experience keeps a dear school, yet fools will learn in no other.", "Benjamin Franklin" },
	{ "To be, or not to be, that is the question.", "William Shakespeare" },
	{ "There is nothing either good or bad, but thinking makes it so.", "William Shakespeare" },
	{ "The course of true love never did run smooth.", "William Shakespeare" },
	{ "Brevity is the soul of wit.", "William Shakespeare" },
	{ "We are such stuff as dreams are made on.", "William Shakespeare" },
	{ "All the world's a stage, and all the men and women merely players.", "William Shakespeare" },
	{ "Neither a borrower nor a lender be.", "William Shakespeare" },
	{ "This above all: to thine own self be true.", "William Shakespeare" },
	{ "All that glisters is not gold.", "William Shakespeare" },
	{ "What's in a name? That which we call a rose by any other name would smell as sweet.", "William Shakespeare" },
	{ "Some are born great, some achieve greatness, and some have greatness thrust upon 'em.", "William Shakespeare" },
	{ "The fault, dear Brutus, is not in our stars, but in ourselves.", "William Shakespeare" },
	{ "Love all, trust a few, do wrong to none.", "William Shakespeare" },
	{ "Better three hours too soon than a minute too late.", "William Shakespeare" },
	{ "How far that little candle throws his beams! So shines a good deed in a naughty world.", "William Shakespeare" },
	{ "Uneasy lies the head that wears a crown.", "William Shakespeare" },
	{ "Parting is such sweet sorrow.", "William Shakespeare" },
	{ "The better part of valour is discretion.", "William Shakespeare" },
	{ "Though she be but little, she is fierce.", "William Shakespeare" },
	{ "Wisely and slow; they stumble that run fast.", "William Shakespeare" },
	{ "If music be the food of love, play on.", "William Shakespeare" },
	{ "There is no darkness but ignorance.", "William Shakespeare" },
	{ "We know what we are, but know not what we may be.", "William Shakespeare" },
	{ "A horse! A horse! My kingdom for a horse!", "William Shakespeare" },
	{ "Lord, what fools these mortals be!", "William Shakespeare" },
	{ "The quality of mercy is not strained.", "William Shakespeare" },
	{ "I wasted time, and now doth time waste me.", "William Shakespeare" },
	{ "Men of few words are the best men.", "William Shakespeare" },
	{ "Things won are done; joy's soul lies in the doing.", "William Shakespeare" },
	{ "A light heart lives long.", "William Shakespeare" },
	{ "Our doubts are traitors, and make us lose the good we oft might win by fearing to attempt.", "William Shakespeare" },
	{ "The unexamined life is not worth living.", "Socrates" },
	{ "The beginning is the most important part of the work.", "Plato" },
	{ "I think, therefore I am.", "Rene Descartes" },
	{ "Our greatest glory is not in never falling, but in rising every time we fall.", "Oliver Goldsmith" },
	{ "Hope is the thing with feathers that perches in the soul.", "Emily Dickinson" },
	{ "Forever is composed of nows.", "Emily Dickinson" },
	{ "It was the best of times, it was the worst of times.", "Charles Dickens" },
	{ "Please, sir, I want some more.", "Charles Dickens" },
	{ "The only way to have a friend is to be one.", "Ralph Waldo Emerson" },
	{ "Nothing great was ever achieved without enthusiasm.", "Ralph Waldo Emerson" },
	{ "To be great is to be misunderstood.", "Ralph Waldo Emerson" },
	{ "Hitch your wagon to a star.", "Ralph Waldo Emerson" },
	{ "A foolish consistency is the hobgoblin of little minds.", "Ralph Waldo Emerson" },
	{ "Every artist was first an amateur.", "Ralph Waldo Emerson" },
	{ "Adopt the pace of nature: her secret is patience.", "Ralph Waldo Emerson" },
	{ "If you have built castles in the air, your work need not be lost; that is where they should be.", "Henry David Thoreau" },
	{ "Our life is frittered away by detail. Simplify, simplify.", "Henry David Thoreau" },
	{ "Rather than love, than money, than fame, give me truth.", "Henry David Thoreau" },
	{ "I went to the woods because I wished to live deliberately.", "Henry David Thoreau" },
	{ "If a man does not keep pace with his companions, perhaps it is because he hears a different drummer.", "Henry David Thoreau" },
	{ "Courage is resistance to fear, mastery of fear, not absence of fear.", "Mark Twain" },
	{ "Always do right. This will gratify some people and astonish the rest.", "Mark Twain" },
	{ "Few things are harder to put up with than the annoyance of a good example.", "Mark Twain" },
	{ "The best way out is always through.", "Robert Frost" },
	{ "Two roads diverged in a wood, and I took the one less traveled by.", "Robert Frost" },
	{ "The woods are lovely, dark and deep, but I have promises to keep.", "Robert Frost" },
	{ "Hope springs eternal in the human breast.", "Alexander Pope" },
	{ "To err is human; to forgive, divine.", "Alexander Pope" },
	{ "A little learning is a dangerous thing.", "Alexander Pope" },
	{ "Fools rush in where angels fear to tread.", "Alexander Pope" },
	{ "Just as the twig is bent, the tree's inclined.", "Alexander Pope" },
	{ "The pen is mightier than the sword.", "Edward Bulwer-Lytton" },
	{ "Happy families are all alike; every unhappy family is unhappy in its own way.", "Leo Tolstoy" },
	{ "Without music, life would be a mistake.", "Friedrich Nietzsche" },
	{ "I'm not afraid of storms, for I'm learning how to sail my ship.", "Louisa May Alcott" },
	{ "'Tis better to have loved and lost than never to have loved at all.", "Alfred Lord Tennyson" },
	{ "A thing of beauty is a joy forever.", "John Keats" },
	{ "Beauty is truth, truth beauty.", "John Keats" },
	{ "Water, water, every where, nor any drop to drink.", "Samuel Taylor Coleridge" },
	{ "I wandered lonely as a cloud that floats on high o'er vales and hills.", "William Wordsworth" },
	{ "The child is father of the man.", "William Wordsworth" },
	{ "Honesty is the first chapter in the book of wisdom.", "Thomas Jefferson" },
	{ "Speak softly and carry a big stick; you will go far.", "Theodore Roosevelt" },
	{ "Four score and seven years ago our fathers brought forth on this continent a new nation.", "Abraham Lincoln" },
	{ "The only thing we have to fear is fear itself.", "Franklin D. Roosevelt" },
	{ "Ask not what your country can do for you; ask what you can do for your country.", "John F. Kennedy" },
	{ "These are the times that try men's souls.", "Thomas Paine" },
	{ "If I have seen further it is by standing on the shoulders of giants.", "Isaac Newton" },
	{ "Into each life some rain must fall.", "Henry Wadsworth Longfellow" },
	{ "Lives of great men all remind us we can make our lives sublime.", "Henry Wadsworth Longfellow" },
	{ "The best laid schemes o' mice an' men gang aft agley.", "Robert Burns" },
	{ "They also serve who only stand and wait.", "John Milton" },
	{ "No man is an island, entire of itself.", "John Donne" },
	{ "To see a world in a grain of sand and a heaven in a wild flower.", "William Blake" },
	{ "It is a truth universally acknowledged, that a single man in possession of a good fortune, must be in want of a wife.", "Jane Austen" },
	{ "Why, sometimes I've believed as many as six impossible things before breakfast.", "Lewis Carroll" },
	{ "Begin at the beginning, and go on till you come to the end: then stop.", "Lewis Carroll" },
	{ "It's no use going back to yesterday, because I was a different person then.", "Lewis Carroll" },
	{ "I can resist everything except temptation.", "Oscar Wilde" },
	{ "Experience is the name every one gives to their mistakes.", "Oscar Wilde" },
	{ "I am large, I contain multitudes.", "Walt Whitman" },
	{ "All that we see or seem is but a dream within a dream.", "Edgar Allan Poe" },
	{ "May you live all the days of your life.", "Jonathan Swift" },
	{ "Variety's the very spice of life, that gives it all its flavour.", "William Cowper" },
	{ "Laugh, and the world laughs with you; weep, and you weep alone.", "Ella Wheeler Wilcox" },
	{ "Do not count your chickens before they are hatched.", "Aesop" },
	{ "Slow but steady wins the race.", "Aesop" },
	{ "United we stand, divided we fall.", "Aesop" },
	{ "Look before you leap.", "Aesop" },
	{ "Familiarity breeds contempt.", "Aesop" },
	{ "Please all, and you will please none.", "Aesop" },
	{ "Self-help is the best help.", "Aesop" },
	{ "It is easy to despise what you cannot get.", "Aesop" },
	{ "Appearances are deceptive.", "Aesop" },
	{ "Gratitude is the sign of noble souls.", "Aesop" },
	{ "Little by little does the trick.", "Aesop" },
	{ "Union gives strength.", "Aesop" },
	{ "Kindness effects more than severity.", "Aesop" },
	{ "Do not trust flatterers.", "Aesop" },
	{ "Little friends may prove great friends.", "Aesop" },
	{ "A liar will not be believed, even when he speaks the truth.", "Aesop" },
	{ "Fall seven times, stand up eight.", "Japanese proverb" },
	{ "Fortune favors the bold.", "Latin proverb" },
};
#define NQUOTES ((int)(sizeof QUOTES / sizeof QUOTES[0]))

/* Traditional proverbs and sayings (no one author). */
static const char *const PROVERBS[] = {
	"Actions speak louder than words.", "Where there is a will, there is a way.", "The early bird catches the worm.",
	"Necessity is the mother of invention.", "Beauty is in the eye of the beholder.", "Rome was not built in a day.",
	"When in Rome, do as the Romans do.", "Many hands make light work.", "All that glitters is not gold.",
	"A penny saved is a penny earned.", "The best time to plant a tree was twenty years ago. The second best time is now.",
	"A smooth sea never made a skilled sailor.", "If you want to go fast, go alone. If you want to go far, go together.",
	"A bird in the hand is worth two in the bush.", "A chain is only as strong as its weakest link.",
	"A fool and his money are soon parted.", "A friend in need is a friend indeed.", "A leopard cannot change its spots.",
	"A picture is worth a thousand words.", "A rolling stone gathers no moss.", "A stitch in time saves nine.",
	"A watched pot never boils.", "Absence makes the heart grow fonder.", "All good things must come to an end.",
	"All roads lead to Rome.", "All's well that ends well.", "An apple a day keeps the doctor away.",
	"April showers bring May flowers.", "As you sow, so shall you reap.", "Barking dogs seldom bite.",
	"Better late than never.", "Better safe than sorry.", "Birds of a feather flock together.",
	"Blood is thicker than water.", "Don't bite the hand that feeds you.", "Don't cross the bridge till you come to it.",
	"Don't cry over spilt milk.", "Don't judge a book by its cover.", "Don't look a gift horse in the mouth.",
	"Don't put all your eggs in one basket.", "Don't throw the baby out with the bathwater.", "Easy come, easy go.",
	"Every cloud has a silver lining.", "Every dog has its day.", "Every rose has its thorn.",
	"Experience is the best teacher.", "Fine feathers make fine birds.", "First things first.",
	"Good things come to those who wait.", "Great minds think alike.", "Half a loaf is better than none.",
	"Haste makes waste.", "He who hesitates is lost.", "He who laughs last laughs longest.", "Home is where the heart is.",
	"Honesty is the best policy.", "Hope for the best, prepare for the worst.",
	"If at first you don't succeed, try, try again.", "If it ain't broke, don't fix it.", "If the shoe fits, wear it.",
	"If wishes were horses, beggars would ride.", "Ignorance is bliss.", "It never rains but it pours.",
	"It takes two to tango.", "It's an ill wind that blows nobody any good.", "It's never too late to learn.",
	"Knowledge is power.", "Laughter is the best medicine.", "Least said, soonest mended.", "Let sleeping dogs lie.",
	"Lightning never strikes twice in the same place.", "Make hay while the sun shines.",
	"Money doesn't grow on trees.", "More haste, less speed.", "No news is good news.", "No pain, no gain.",
	"Nothing ventured, nothing gained.", "Old habits die hard.", "One good turn deserves another.",
	"One man's trash is another man's treasure.", "Out of sight, out of mind.", "Patience is a virtue.",
	"Practice makes perfect.", "Prevention is better than cure.", "Pride comes before a fall.", "Seeing is believing.",
	"Silence is golden.", "Strike while the iron is hot.", "The apple doesn't fall far from the tree.",
	"The best things in life are free.", "The bigger they are, the harder they fall.",
	"The grass is always greener on the other side of the fence.", "The squeaky wheel gets the grease.",
	"The proof of the pudding is in the eating.", "There's no place like home.",
	"There's no such thing as a free lunch.", "There is no time like the present.", "Time and tide wait for no man.",
	"Time flies when you're having fun.", "Too many cooks spoil the broth.", "Two heads are better than one.",
	"Two wrongs don't make a right.", "Waste not, want not.", "What goes around comes around.",
	"When the cat's away, the mice will play.", "You can lead a horse to water, but you can't make it drink.",
	"You can't have your cake and eat it too.", "You can't teach an old dog new tricks.",
	"A bad workman always blames his tools.", "A cat has nine lives.", "A good beginning makes a good ending.",
	"A man is known by the company he keeps.", "A miss is as good as a mile.", "A new broom sweeps clean.",
	"A problem shared is a problem halved.", "A sound mind in a sound body.", "After a storm comes a calm.",
	"All work and no play makes Jack a dull boy.", "Empty vessels make the most noise.", "Any port in a storm.",
	"Ask no questions and hear no lies.", "Bad news travels fast.", "Beggars can't be choosers.",
	"Better to light a candle than to curse the darkness.", "Great oaks from little acorns grow.",
	"Charity begins at home.", "Clothes make the man.", "Curiosity killed the cat.", "Do as I say, not as I do.",
	"Don't burn your bridges behind you.", "Don't make a mountain out of a molehill.", "Don't rock the boat.",
	"People who live in glass houses shouldn't throw stones.", "East or west, home is best.",
	"Every little bit helps.", "Every picture tells a story.", "Failing to plan is planning to fail.",
	"Fight fire with fire.", "Finders keepers, losers weepers.", "Forewarned is forearmed.",
	"Give credit where credit is due.", "Give an inch and they'll take a mile.", "Good fences make good neighbors.",
	"Hard work never hurt anyone.", "He who pays the piper calls the tune.", "Hindsight is always twenty-twenty.",
	"History repeats itself.", "You catch more flies with honey than with vinegar.",
	"If you can't beat them, join them.", "If you want something done right, do it yourself.",
	"In for a penny, in for a pound.", "It takes all sorts to make a world.", "It's the thought that counts.",
	"Jack of all trades, master of none.", "Learn to walk before you run.", "Leave well enough alone.",
	"Let bygones be bygones.", "Life is what you make it.", "Like father, like son.", "Live and learn.",
	"Live and let live.", "Love conquers all.", "Love is blind.", "Many a little makes a mickle.",
	"March comes in like a lion and goes out like a lamb.", "Misery loves company.", "Moderation in all things.",
	"Money talks.", "Never say never.", "Oil and water don't mix.", "Old friends are best.",
	"Once bitten, twice shy.", "One swallow does not make a summer.", "Opportunity seldom knocks twice.",
	"Out of the frying pan and into the fire.", "Practice what you preach.", "Put your best foot forward.",
	"Saying is one thing and doing another.", "Still waters run deep.", "Talk is cheap.",
	"The best defense is a good offense.", "The darkest hour is just before the dawn.", "The more the merrier.",
	"The pot calls the kettle black.", "The truth will out.", "There are plenty more fish in the sea.",
	"Where there's smoke, there's fire.", "There is safety in numbers.", "Third time lucky.", "Tomorrow is another day.",
	"You can have too much of a good thing.", "Truth is stranger than fiction.", "Two's company, three's a crowd.",
	"Walls have ears.", "Well begun is half done.", "What's done is done.", "While there's life, there's hope.",
	"You can't make an omelette without breaking eggs.", "You can't please everyone.", "You win some, you lose some.",
	"Beauty is only skin deep.", "Busy hands are happy hands.", "Count your blessings.",
	"Don't change horses in midstream.", "Don't put the cart before the horse.", "Hunger is the best sauce.",
	"Little and often fills the purse.", "Manners make the man.", "Measure twice, cut once.",
	"Penny wise and pound foolish.", "Rain before seven, fine before eleven.",
	"Red sky at night, sailor's delight. Red sky in the morning, sailor's warning.", "Speech is silver, silence is golden.",
	"What you see is what you get.", "You never know what you can do till you try.", "A good name is better than riches.",
	"A word to the wise is enough.", "All cats are grey in the dark.", "Constant dripping wears away the stone.",
	"Fair exchange is no robbery.", "Fine words butter no parsnips.", "Forgive and forget.",
	"Handsome is as handsome does.", "It's a small world.", "Keep your chin up.", "Civility costs nothing.",
	"Never trouble trouble till trouble troubles you.", "No bees, no honey; no work, no money.",
	"Nothing succeeds like success.", "One step at a time.", "Possession is nine-tenths of the law.",
	"Sticks and stones may break my bones, but words will never hurt me.", "Good things come in small packages.",
	"The shoemaker's children go barefoot.", "There's a time and a place for everything.", "Time heals all wounds.",
	"Tomorrow never comes.", "What can't be cured must be endured.", "You can't get blood from a stone.",
	"You get what you pay for.", "A closed mouth catches no flies.", "A creaking door hangs longest.",
	"A fair day's wage for a fair day's work.", "A hungry man is an angry man.", "A man's home is his castle.",
	"A place for everything, and everything in its place.", "Accidents will happen.", "All is fair in love and war.",
	"Cut your coat according to your cloth.", "Don't bite off more than you can chew.",
	"Don't let the grass grow under your feet.", "Don't upset the apple cart.", "Every path has a puddle.",
	"Every tide has its ebb.", "Faint heart never won fair lady.", "First come, first served.",
	"Great talkers are little doers.", "Hasty climbers have sudden falls.", "It's easy to be wise after the event.",
	"Many a true word is spoken in jest.", "Nothing comes from nothing.", "One rotten apple spoils the barrel.",
	"Slow but sure.", "The best of friends must part.", "The exception proves the rule.",
	"The first step is always the hardest.", "The longest way round is the shortest way home.",
	"There's many a slip between the cup and the lip.", "To each his own.", "Wonders will never cease.",
	"You can't run with the hare and hunt with the hounds.", "Little pitchers have big ears.",
	"A good conscience is a soft pillow.", "After dinner rest a while, after supper walk a mile.",
	"Better bend than break.", "Better to be alone than in bad company.", "Every man to his trade.",
	"Make the best of a bad job.", "It's never too late to mend.", "Out of debt, out of danger.",
	"Short reckonings make long friends.", "The bread never falls but on its buttered side.",
	"The cat would eat fish but would not wet her feet.", "Wise men learn by other men's mistakes; fools by their own.",
	"You can't put old heads on young shoulders.", "He that would eat the fruit must climb the tree.",
	"The pitcher goes so often to the well that it is broken at last.", "Even a worm will turn.",
	"Many drops make a shower.", "Every man is the architect of his own fortune.",
	"Fortune knocks at least once at every door.", "Good words cost nothing.", "Save something for a rainy day.",
	"Learning is a treasure that follows its owner everywhere.", "Look on the bright side.",
	"Small rain lays great dust.", "The night brings counsel.", "Where the river is deepest it makes the least noise.",
	"A full cup must be carried steadily.", "One kind word can warm three winter months.",
};
#define NPROVERBS ((int)(sizeof PROVERBS / sizeof PROVERBS[0]))
#define NFIXED (NQUOTES + NPROVERBS)

/* ---- made-up sentences: a small grammar with word lists (space-separated; '_' joins a phrase; "sg/pl" gives an
 * irregular plural). Each template slot is a letter code in braces; {a} puts "a" or "an" before the next word. ---- */
static const char W_ADJ[] = /* for animals and people */
	"brave busy calm cheerful chilly clever clumsy curious dizzy eager elegant enormous fancy fluffy friendly frosty "
	"gentle gigantic graceful grumpy happy helpful humble hungry jolly jumpy kind lazy little lively lonely lucky merry "
	"mighty modest muddy mysterious nervous noisy old patient playful plump polite proud quiet restless royal shy silly "
	"sleepy sneaky speedy spotted striped stubborn swift thirsty tiny wise wobbly young zany";
static const char W_OADJ[] = /* for things */
	"ancient blue bright crooked dusty elegant enormous fancy giant glowing golden green heavy lucky magic muddy "
	"mysterious noisy old orange purple red royal rusty shiny silver sparkly spotted squeaky striped tiny wobbly wooden "
	"yellow";
static const char W_ANIMAL[] =
	"badger bear beaver beetle camel crab deer/deer donkey dragon duck eagle elephant ferret flamingo fox frog giraffe "
	"goat goose/geese gorilla hamster hedgehog hippo kangaroo kitten koala lion llama lobster mole monkey moose/moose "
	"mouse/mice octopus otter owl panda parrot peacock pelican penguin pig pony porcupine puppy rabbit raccoon robin "
	"seal sheep/sheep snail spider squirrel swan tiger toad tortoise turtle walrus whale wolf/wolves zebra";
static const char W_PERSON[] =
	"astronaut baker captain chef clown cook dancer detective doctor explorer farmer firefighter gardener giant inventor "
	"juggler king knight librarian magician musician neighbor painter pilot pirate poet princess queen robot sailor "
	"scientist shepherd singer tailor teacher wizard";
static const char W_RELATIVE[] =
	"aunt uncle cousin neighbor grandmother grandfather teacher best_friend little_brother big_sister";
static const char W_PLACE[] =
	"attic bakery barn beach bridge castle cave cellar circus desert farm forest garden greenhouse harbor hill island "
	"jungle kitchen lake library lighthouse market meadow moon mountain museum orchard palace park playground pond "
	"river school station swamp tower valley village volcano waterfall windmill zoo";
static const char W_PREP[] = "behind beside near around past across";
static const char W_VI[] =
	"danced sang giggled napped wandered tiptoed waltzed skipped hopped marched paddled sneezed snored whistled "
	"wobbled yawned rested hid waited splashed raced rolled tumbled twirled bounced galloped scurried strolled "
	"floated hummed laughed shivered wiggled zoomed camped jogged swam slept juggled sulked fished";
static const char W_VT[] =
	"painted found built borrowed carried chased counted delivered discovered dropped fixed hid hugged lost mended "
	"opened packed polished rescued sold stacked tickled traded washed wrapped collected decorated followed guarded "
	"measured noticed planted sketched visited";
static const char W_VF[] =
	"ate baked sold counted shared peeled stacked hid collected delivered washed carried juggled munched nibbled "
	"packed picked tossed";
static const char W_VB[] = "tickle chase borrow race wake follow feed tease trust surprise interrupt hug";
static const char W_VBI[] =
	"dance sing nap hide wait hop march paddle sneeze snore whistle wobble yawn skip tiptoe giggle camp swim sleep "
	"juggle sulk fish";
static const char W_GIVE[] = "gave sold lent showed mailed handed sent threw passed brought";
static const char W_OBJ[] =
	"anchor balloon basket bell bicycle blanket boat book bucket button candle clock compass crown drum feather fiddle "
	"hat helmet kite ladder lantern letter map mirror necklace paintbrush pillow puzzle rocket rope scarf shovel sock "
	"spoon suitcase teapot telescope trophy trumpet umbrella violin wagon whistle";
static const char W_FOOD[] =
	"apples bananas berries biscuits carrots cherries cookies crackers cupcakes dumplings grapes lemons muffins noodles "
	"onions oranges pancakes peaches pears pickles pies plums potatoes pretzels pumpkins radishes raisins sandwiches "
	"strawberries tomatoes turnips waffles walnuts";
static const char W_TIME[] =
	"last_night at_noon at_midnight at_dawn before_breakfast after_lunch after_dinner in_the_rain during_the_storm "
	"after_school last_summer yesterday on_a_snowy_day under_the_stars at_sunset in_the_fog on_a_windy_day at_bedtime";
static const char W_TIMEB[] =
	"before_breakfast after_dark at_bedtime in_the_rain during_a_storm before_lunch after_dinner on_a_Monday "
	"at_midnight in_the_fog";
static const char W_DAY[] = "Monday Tuesday Wednesday Thursday Friday Saturday Sunday";
static const char W_NUM[] =
	"two three four five six seven eight nine ten eleven twelve twenty thirty forty fifty sixty a_hundred a_dozen";
static const char W_UNTIL[] =
	"midnight sunrise lunchtime dinnertime the_rain_stopped the_moon_came_out the_bus_arrived the_cows_came_home "
	"the_bell_rang the_sun_went_down";

static const char *const TEMPLATES[] = {
	"The {A} {N} {V} {P} the {L} {T}.",
	"{U} {A} {M} {F} {U} {D} {T}.",
	"My {R} {W} {a}{C} {O} {P} the {L}.",
	"Never {B} {a}{A} {N} {E}.",
	"Why did the {A} {N} {I} {P} the {L}?",
	"{a}{A} {H} and {a}{A} {N} {V} {P} the {L}.",
	"{U} {M} and {U} {M} {V} {P} the {L} {T}.",
	"The {H} {W} {a}{C} {O} and {G} it to the {A} {N}.",
	"Nobody knows why the {H} {F} {U} {D} {P} the {L}.",
	"On {Y}, the {A} {H} {V} {P} the {L} with {U} {M}.",
	"{U} {A} {K} {V} {P} the {L} until {X}.",
	"Last {Y}, my {R} {V} {P} the {L} with {a}{A} {N}.",
	"The {A} {K} {W} {U} {Q} {T}.",
	"Who {W} the {C} {O} {P} the {L}?",
	"{a}{A} {N} {V} {P} the {L}, and nobody noticed.",
	"The {H} and the {N} {V} {P} the {L} {T}.",
};
#define NTEMPLATES ((int)(sizeof TEMPLATES / sizeof TEMPLATES[0]))

/* the k-th word of a list (0 = count them); returns the count */
static int list_word(const char *list, int k, char *out, int plural) {
	int n = 0;
	for (const char *p = list; *p;) {
		while (*p == ' ') p++;
		if (!*p) break;
		const char *e = p;
		while (*e && *e != ' ') e++;
		if (out && n == k) {
			const char *sl = memchr(p, '/', (size_t)(e - p));
			int len;
			if (sl && plural) { len = (int)(e - sl - 1); memcpy(out, sl + 1, (size_t)len); }
			else { len = (int)((sl ? sl : e) - p); memcpy(out, p, (size_t)len); }
			out[len] = 0;
			if (plural && !sl) { /* regular plural */
				char last = out[len - 1], prev = len > 1 ? out[len - 2] : 0;
				if (last == 's' || last == 'x' || last == 'z' || (last == 'h' && (prev == 'c' || prev == 's'))) strcat(out, "es");
				else if (last == 'y' && !strchr("aeiou", prev)) strcpy(out + len - 1, "ies");
				else strcat(out, "s");
			}
			for (char *c = out; *c; c++) if (*c == '_') *c = ' ';
		}
		n++;
		p = e;
	}
	return n;
}

static void make_sentence(GameCtx *g, char *out, int cap) {
	for (int attempt = 0; attempt < 20; attempt++) {
		const char *t = TEMPLATES[g_rand(g, NTEMPLATES)];
		char buf[256];
		int n = 0, article = 0;
		while (*t && n < (int)sizeof buf - 40) {
			if (*t != '{') { buf[n++] = *t++; continue; }
			char code = t[1];
			t += 3;
			if (code == 'a') { article = 1; continue; }
			const char *list = 0;
			int plural = 0;
			switch (code) {
			case 'A': list = W_ADJ; break;
			case 'C': list = W_OADJ; break;
			case 'N': list = W_ANIMAL; break;
			case 'M': list = W_ANIMAL; plural = 1; break;
			case 'H': list = W_PERSON; break;
			case 'K': list = W_PERSON; plural = 1; break;
			case 'R': list = W_RELATIVE; break;
			case 'L': list = W_PLACE; break;
			case 'P': list = W_PREP; break;
			case 'V': list = W_VI; break;
			case 'W': list = W_VT; break;
			case 'F': list = W_VF; break;
			case 'B': list = W_VB; break;
			case 'I': list = W_VBI; break;
			case 'G': list = W_GIVE; break;
			case 'O': list = W_OBJ; break;
			case 'Q': list = W_OBJ; plural = 1; break;
			case 'D': list = W_FOOD; break;
			case 'T': list = W_TIME; break;
			case 'E': list = W_TIMEB; break;
			case 'Y': list = W_DAY; break;
			case 'U': list = W_NUM; break;
			case 'X': list = W_UNTIL; break;
			}
			char w[40];
			if (!list) continue;
			list_word(list, g_rand(g, list_word(list, 0, 0, 0)), w, plural);
			if (article) { const char *a = strchr("aeiou", w[0]) ? "an " : "a "; memcpy(buf + n, a, strlen(a)); n += (int)strlen(a); article = 0; }
			memcpy(buf + n, w, strlen(w));
			n += (int)strlen(w);
		}
		buf[n] = 0;
		if (n <= cap) { memcpy(out, buf, (size_t)n + 1); return; }
	}
	snprintf(out, (size_t)cap + 1, "The sleepy owl danced under the moon.");
}

/* Which text comes next: half the games take a quote or proverb from a bag kept on the Kindle (bag.h: none comes back
 * until all have been played, and none within 100 of its last time even across rounds), the other half a made-up
 * sentence. */
typedef char fixed_fits[NFIXED <= BAG_MAX ? 1 : -1];
static int next_fixed(GameCtx *g) {
	static uint32_t hash[NFIXED];
	if (!hash[0])
		for (int i = 0; i < NFIXED; i++) hash[i] = bag_hash(i < NQUOTES ? QUOTES[i].text : PROVERBS[i - NQUOTES]);
	return bag_next(g, "texts", hash, NFIXED, 100);
}

typedef struct {
	char plain[MAXQ + 1]; /* first three fields are read by the host test */
	char enc[26];         /* plain letter -> code letter */
	int q, len, sel, lvl; /* q: index into QUOTES then PROVERBS, -1 for a made-up sentence */
	char guess[26];       /* by code letter: the guessed plain letter, 0 = none */
	unsigned char fixed[26], present[26];
	short bx[MAXQ], by[MAXQ]; /* top-left of each letter box / punctuation mark; -1 for spaces */
	int bw, cpx, gpx, pw, textbottom;
	int kx, ky, kw, kh, kgap;
	char msg[160];
} S;

static int is_letter(char c) { return c >= 'A' && c <= 'Z'; }
static int code_of(S *s, int i) { return s->enc[s->plain[i] - 'A'] - 'A'; }

/* --- layout --- */
static int lay_out(S *s, int bw, int ax, int aw, int ty, int th) { /* returns 1 if it fits */
	int gap = bw / 8, sp = bw * 55 / 100, maxw = aw - 2 * M, cpx = bw * 36 / 100, lineh = bw + cpx + 12 + bw / 3;
	int pw = bw * 45 / 100, linew[MAXQ], nl = 0, x = 0, i = 0;
	int lineof[MAXQ], xin[MAXQ];
	while (i < s->len) {
		if (s->plain[i] == ' ') { i++; continue; }
		int j = i, w = 0;
		while (j < s->len && s->plain[j] != ' ') { w += is_letter(s->plain[j]) ? bw + gap : pw; j++; }
		if (w > maxw) return 0;
		if (x > 0 && x + w > maxw) { linew[nl++] = x - sp; x = 0; }
		for (int k = i; k < j; k++) { lineof[k] = nl; xin[k] = x; x += is_letter(s->plain[k]) ? bw + gap : pw; }
		x += sp;
		i = j;
	}
	linew[nl++] = x - sp;
	if (nl * lineh > th) return 0;
	int y0 = ty + (th - nl * lineh) / 2;
	for (int k = 0; k < s->len; k++) {
		if (s->plain[k] == ' ') { s->bx[k] = s->by[k] = -1; continue; }
		s->bx[k] = (short)(ax + (aw - linew[lineof[k]]) / 2 + xin[k]);
		s->by[k] = (short)(y0 + lineof[k] * lineh);
	}
	s->bw = bw; s->cpx = cpx; s->gpx = bw * 60 / 100; s->pw = pw;
	s->textbottom = y0 + nl * lineh;
	return 1;
}

/* --- drawing --- */
static void draw_box(GameCtx *g, S *s, int i) {
	int x = s->bx[i], y = s->by[i], ci = code_of(s, i);
	g_clear(g, x + 3, y + 3, s->bw - 6, s->bw - 6);
	if (ci == s->sel) g_box(g, x + 4, y + 4, s->bw - 8, s->bw - 8, 215);
	if (s->guess[ci]) { char t[2] = { s->guess[ci], 0 }; g_print(g, x, y, s->bw, s->bw, s->gpx, s->fixed[ci], t); }
}

static void draw_code(GameCtx *g, S *s, int ci) { for (int i = 0; i < s->len; i++) if (is_letter(s->plain[i]) && code_of(s, i) == ci) draw_box(g, s, i); }

static void key_box(S *s, int k, int *x, int *y, int *w) { /* 0..25 letters, 26 Erase */
	if (k == 26) { *x = s->kx + 5 * (s->kw + s->kgap); *y = s->ky + 2 * (s->kh + s->kgap); *w = 3 * s->kw + 2 * s->kgap; return; }
	*x = s->kx + (k % 13) * (s->kw + s->kgap);
	*y = s->ky + (k / 13) * (s->kh + s->kgap);
	*w = s->kw;
}

static int used(S *s, int letter) { for (int c = 0; c < 26; c++) if (s->guess[c] == 'A' + letter) return 1; return 0; }

static void draw_key(GameCtx *g, S *s, int k, int frame) {
	int x, y, w;
	key_box(s, k, &x, &y, &w);
	if (frame) g_frame(g, x, y, w, s->kh, 3);
	else g_clear(g, x + 3, y + 3, w - 6, s->kh - 6);
	if (k < 26 && used(s, k)) g_box(g, x + 4, y + 4, w - 8, s->kh - 8, 200);
	char t[2] = { (char)('A' + k), 0 };
	g_print(g, x, y, w, s->kh, k == 26 ? 40 : s->kh * 50 / 100, 0, k == 26 ? "Erase" : t);
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	s->lvl = g_level(g);
	char made[MAXQ + 1];
	const char *t = made;
	if (g_rand(g, 2)) {
		s->q = next_fixed(g);
		t = s->q < NQUOTES ? QUOTES[s->q].text : PROVERBS[s->q - NQUOTES];
	} else {
		s->q = -1;
		make_sentence(g, made, MAXQ);
	}
	s->len = 0;
	for (; *t && s->len < MAXQ; t++) s->plain[s->len++] = *t >= 'a' && *t <= 'z' ? (char)(*t - 32) : *t;
	s->plain[s->len] = 0;
	/* a random swap with no letter standing for itself */
	for (int ok = 0; !ok;) {
		for (int i = 0; i < 26; i++) s->enc[i] = (char)('A' + i);
		for (int i = 25; i > 0; i--) { int j = g_rand(g, i + 1); char c = s->enc[i]; s->enc[i] = s->enc[j]; s->enc[j] = c; }
		ok = 1;
		for (int i = 0; i < 26; i++) if (s->enc[i] == 'A' + i) ok = 0;
	}
	int npresent = 0;
	for (int i = 0; i < s->len; i++)
		if (is_letter(s->plain[i]) && !s->present[code_of(s, i)]) { s->present[code_of(s, i)] = 1; npresent++; }
	int give = s->lvl == 0 ? 3 + g_rand(g, 2) : s->lvl == 1 ? 1 + g_rand(g, 2) : 0;
	for (int k = 0; k < give && k < npresent - 1; k++) {
		int ci;
		do ci = g_rand(g, 26); while (!s->present[ci] || s->fixed[ci]);
		s->fixed[ci] = 1;
		for (int p = 0; p < 26; p++) if (s->enc[p] - 'A' == ci) s->guess[ci] = (char)('A' + p);
	}

	/* keypad: A-M, N-Z and Erase at the bottom; the quote in the space above (with a line for the author) */
	s->kgap = 10;
	s->kw = (aw - 2 * M - 12 * s->kgap) / 13;
	if (s->kw > 125) s->kw = 125;
	s->kh = s->kw < 110 ? s->kw : 110;
	s->kx = ax + (aw - (13 * s->kw + 12 * s->kgap)) / 2;
	s->ky = ay + ah - M - (3 * s->kh + 2 * s->kgap);
	int ty = ay + M, th = s->ky - 40 - 70 - ty;
	static const int BW[] = { 120, 110, 100, 92, 84, 76, 70, 64, 58, 52, 46, 40 };
	for (int i = 0; i < (int)(sizeof BW / sizeof BW[0]); i++) if (lay_out(s, BW[i], ax, aw, ty, th)) break;

	s->sel = -1;
	for (int i = 0; i < s->len && s->sel < 0; i++) if (is_letter(s->plain[i]) && !s->fixed[code_of(s, i)]) s->sel = code_of(s, i);
	g_speed(g, 30);
	for (int i = 0; i < s->len; i++) {
		if (s->bx[i] < 0) continue;
		if (!is_letter(s->plain[i])) { char p[2] = { s->plain[i], 0 }; g_print(g, s->bx[i], s->by[i], s->pw, s->bw, s->gpx, 0, p); continue; }
		char c[2] = { s->enc[s->plain[i] - 'A'], 0 };
		g_frame(g, s->bx[i], s->by[i], s->bw, s->bw, 3);
		g_print(g, s->bx[i], s->by[i] + s->bw + 2, s->bw, s->cpx + 8, s->cpx, 0, c);
		if (s->guess[code_of(s, i)] || code_of(s, i) == s->sel) draw_box(g, s, i);
	}
	for (int k = 0; k <= 26; k++) draw_key(g, s, k, 1);
	g_remark(g, s->lvl == 2 ? "Crack the code - no letters given!" : "Crack the code! Each letter stands for another.");
	g_say(g, "tap a box, then the letter you think it is");
}

/* --- playing --- */
static void select_code(GameCtx *g, S *s, int ci) {
	int old = s->sel;
	s->sel = ci;
	if (old >= 0) draw_code(g, s, old);
	if (ci >= 0) draw_code(g, s, ci);
}

static int first_box(S *s, int ci) { for (int i = 0; i < s->len; i++) if (is_letter(s->plain[i]) && code_of(s, i) == ci) return i; return -1; }

/* the next code letter (reading on from code letter ci) that has no guess yet, or -1 */
static int next_open(S *s, int ci) {
	int from = ci >= 0 ? first_box(s, ci) : 0;
	for (int k = 1; k <= s->len; k++) {
		int i = (from + k) % s->len;
		if (is_letter(s->plain[i]) && !s->guess[code_of(s, i)]) return code_of(s, i);
	}
	return -1;
}

static void check(GameCtx *g, S *s) {
	int wrong = 0, empty = 0;
	for (int c = 0; c < 26; c++) {
		if (!s->present[c]) continue;
		if (!s->guess[c]) empty++;
		else if (s->enc[s->guess[c] - 'A'] != 'A' + c) wrong++;
	}
	if (empty) return;
	if (!wrong) {
		int ax, ay, aw, ah;
		g_area(g, &ax, &ay, &aw, &ah);
		select_code(g, s, -1);
		g_speed(g, 4);
		const char *author = s->q < 0 ? "a sentence I made up" : s->q < NQUOTES ? QUOTES[s->q].author : "Proverb";
		snprintf(s->msg, sizeof s->msg, "- %s", author);
		g_print(g, ax + M, s->textbottom + 5, aw - 2 * M, 60, 44, 0, s->msg);
		g_remark(g, s->msg);
		if (s->q < 0) snprintf(s->msg, sizeof s->msg, "Solved! A sentence I made up.");
		else if (!strcmp(author, "Proverb") || strstr(author, "proverb")) snprintf(s->msg, sizeof s->msg, "Solved! (%s)", author);
		else snprintf(s->msg, sizeof s->msg, "Solved! A quote by %s.", author);
		g_over(g, s->msg);
		return;
	}
	if (s->lvl == 0) g_say(g, "every box is filled, but %d letter%s wrong", wrong, wrong == 1 ? " is" : "s are");
	else g_say(g, "every box is filled, but something's not right");
}

static void set_guess(GameCtx *g, S *s, int ci, int letter) { /* letter 0 = erase */
	if (letter) {
		for (int c = 0; c < 26; c++) {
			if (c == ci || s->guess[c] != letter) continue;
			if (s->fixed[c]) { g_say(g, "%c is already given", letter); return; }
			s->guess[c] = 0; /* moved from another code letter */
			draw_code(g, s, c);
		}
	}
	int old = s->guess[ci];
	s->guess[ci] = (char)letter;
	draw_code(g, s, ci);
	if (old) draw_key(g, s, old - 'A', 0);
	if (letter) draw_key(g, s, letter - 'A', 0);
	if (letter) {
		int nx = next_open(s, ci);
		if (nx >= 0) select_code(g, s, nx);
		g_say(g, "%c = %c%s", 'A' + ci, letter, nx >= 0 ? "" : " - all letters guessed");
	} else g_say(g, "erased %c", 'A' + ci);
	check(g, s);
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	/* the state is complete from the start, so taps while the board is still being drawn are fine */
	int x = st->cx, y = st->cy;
	if (!st->tap && !st->finger) {
		if (st->x1 - st->x0 > 60 || st->y1 - st->y0 > 60) { g_say(g, "just tap - a box, then a letter"); return; }
		x = (st->x0 + st->x1) / 2; y = (st->y0 + st->y1) / 2;
	}
	g_speed(g, 12);
	for (int k = 0; k <= 26; k++) {
		int kx, ky, kw;
		key_box(s, k, &kx, &ky, &kw);
		if (x < kx || x >= kx + kw || y < ky || y >= ky + s->kh) continue;
		if (s->sel < 0) { g_say(g, "tap a box first"); return; }
		if (k == 26) {
			if (!s->guess[s->sel]) { g_say(g, "nothing to erase there"); return; }
			set_guess(g, s, s->sel, 0);
		} else set_guess(g, s, s->sel, 'A' + k);
		return;
	}
	for (int i = 0; i < s->len; i++) {
		if (!is_letter(s->plain[i]) || x < s->bx[i] || x >= s->bx[i] + s->bw || y < s->by[i] || y >= s->by[i] + s->bw + s->cpx + 10) continue;
		int ci = code_of(s, i);
		if (s->fixed[ci]) { g_say(g, "that letter is given"); return; }
		if (ci != s->sel) select_code(g, s, ci);
		g_say(g, "now tap the letter for %c", 'A' + ci);
		return;
	}
	g_say(g, "tap a box, then a letter");
}

const GameDef GAME_cryptogram = { "cryptogram", "Cryptogram", "Crack the code to reveal a famous quote", sizeof(S), 0, 1, start, stroke, 0 };
