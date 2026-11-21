CHECKS += check-hare
CHECKS_LINUX_SKIP += check-hare
#| nothing on the file server is reachable without the PIN; the page is named after its link
#| (`/link.js`: the cable's or Wi-Fi's, never kept by a browser), and a web folder from before that
#| still serves its page; an upload or an update's file of an exact multiple of 128 KiB sent with
#| the page's trailer is stored to the byte, one sent plainly as sent, and one with a trailer it
#| cannot make sense of refused, the file of its name kept and no part-file left; an upload cut
#| halfway goes on from where it stopped to the byte (a wrong offset 409 with X-Upload-Have,
#| another file under the same name, a broken writer taken over, the trailer on a resumed tail, the
#| 30-minute keep on a fake clock, DELETE, File Transfer closed and opened again); a game in a
#| folder of games listed with its console's cover, and a listing of `.media` with no covers named
#| for its pictures
