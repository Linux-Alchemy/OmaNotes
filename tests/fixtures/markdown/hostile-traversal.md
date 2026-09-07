# Path traversal must stay inside the root

![parent escape](../outside.png)

![deep escape](../../../../etc/hostname)

![absolute path](/etc/hostname)

![home path](~/.ssh/id_rsa)

![file scheme image](file:///etc/hostname)

[file scheme link](file:///etc/passwd)

[parent link](../outside.md)

[absolute link](/etc/passwd)

Dot games: ![dots](assets/../../escape.png) and ![encoded](..%2F..%2Fescape.png)

Text after the attempts must still be visible.
