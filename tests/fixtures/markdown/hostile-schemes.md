# Every scheme outside the allowlist must be refused

[javascript link](javascript:alert('js'))

[data html link](data:text/html,<script>alert('data')</script>)

![data image](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==)

[vscode handler](vscode://payload/open?arg=--dangerous)

[ssh handler](ssh://evil.example/)

[magnet handler](magnet:?xt=urn:btih:payload)

[mailto](mailto:someone@example.com)

[custom scheme](omanotes-evil://do-things)

[case games](JaVaScRiPt:alert('case'))

[whitespace games](java script:alert('ws'))

Allowed for contrast, and must still work: [https link](https://omarchy.org).
