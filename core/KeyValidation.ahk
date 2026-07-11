; ZhanFa shot keys must be independent number-pad keys.
ZhanFaIsNumpadKey(key){
    return RegExMatch(key, "^Numpad(?:[0-9]|Dot|Div|Mult|Add|Sub|Enter|Del|Ins|End|Down|PgDn|Left|Clear|Right|Home|Up|PgUp)$")
}
