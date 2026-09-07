# Raw HTML must render as inert text

<script>alert('executed');</script>

<script src="https://evil.example/payload.js"></script>

<iframe src="https://evil.example/frame"></iframe>

<img src="x" onerror="alert('img onerror')">

<a href="javascript:alert('href')" onclick="alert('onclick')">click me</a>

<div style="position:fixed;inset:0;background:red">overlay attempt</div>

<object data="https://evil.example/o"></object>
<embed src="https://evil.example/e">

Entity smuggling: &lt;script&gt;alert('entities')&lt;/script&gt;

Text after the HTML must still be visible, proving rendering continued.
