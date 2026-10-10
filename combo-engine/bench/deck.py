"""Test deck: Elemental/Vision/Destiny HERO (approximate Master Duel style list, 40 main / 15 extra)."""
STRATOS, FARIS, VYON, INCREASE = 40044918, 18094166, 27780618, 22865492
MALICIOUS, SHADOW_MIST, ECALL, AHL = 9411399, 50720316, 213326, 8949584
POLY, FDESTINY, SUNRISE, DPE, NEOS = 24094653, 52947044, 22908820, 60461804, 89943723
LIQUID, DIAMOND, HONEST, DECIDER, DENIER, MASKCHANGE = 59392529, 13093792, 14124483, 64184058, 16605586, 21143940
ASH, IMPERM, MAXXC, VEILER, CBTG = 14558127, 10045474, 23434538, 97268402, 24224830

MAIN = ([STRATOS] + [FARIS] * 3 + [VYON] + [INCREASE] + [MALICIOUS] * 2 + [SHADOW_MIST] * 2 + [LIQUID] * 2 +
        [DIAMOND, NEOS, HONEST, DECIDER, DENIER] + [ECALL] * 2 + [AHL] + [POLY] * 2 + [FDESTINY] * 3 + [MASKCHANGE] +
        [ASH] * 3 + [IMPERM] * 3 + [MAXXC] * 3 + [VEILER] * 3 + [CBTG] * 2)
assert len(MAIN) == 40, len(MAIN)

EXTRA = [SUNRISE, DPE, DPE, 30757127, 90579153, 40854197, 3642509, 33574806, 45170821, 46759931, 58481572,
         1948619, 58004362, 63813056, 19324993]  # Dangerous, Dystopia, Abs Zero, Great Tornado, Escuridao, Adoration, Trinity, Dark Law, Xtra HEROs


def split(hand):
    """Remove hand cards from MAIN; pad hand with blanks (Imperm) are already in the list."""
    deck = list(MAIN)
    for c in hand:
        deck.remove(c)
    return deck
