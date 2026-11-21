CHECKS += check-signin
CHECKS_LINUX_SKIP += check-signin
#| RetroAchievements sign-in against a stand-in for curl: a token signs in; a 401 says the user or
#| password is wrong; a 403, a 503 or a redirect says retroachievements.org would not sign you in,
#| with the number; a reply cut short, too long, or nothing at all is never read as a token; and as
#| curl, the check holds that the password is never on its command line, every file the request
#| makes is new and 0600 from the start and gone after, and the log names the failure and no
#| credential
