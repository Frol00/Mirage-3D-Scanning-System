param(
    [Parameter(Mandatory=$true)]
    [string]$RaspberryIp,

    [int]$PcControlPort = 5055,

    [string]$Protocol = "http",

    [string]$Token = ""
)

$headers = @{}
if ($Token -ne "") {
    $headers["X-Mirage-Token"] = $Token
}

$body = @{
    port = $PcControlPort
    protocol = $Protocol
} | ConvertTo-Json

Invoke-RestMethod `
    -Method Post `
    -Uri "http://$RaspberryIp`:8091/register" `
    -ContentType "application/json" `
    -Headers $headers `
    -Body $body
