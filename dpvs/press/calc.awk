# echo "486140 442092543 860834439 35193297957 100689756124" | awk -f calc.awk
BEGIN {
    OFS = " | ";
}
{
    inPkts   = $2 / 10^3;
    outPkts  = $3 / 10^3;
    inBytes  = $4 / 10^6;
    outBytes = $5 / 10^6;
    pps = (inPkts + outPkts) / 60;
    bps = (inBytes + outBytes) / 60 * 8;
    print $1, int(inPkts + 0.5), int(outPkts + 0.5), int(inBytes + 0.5), int(outBytes + 0.5), int(pps + 0.5), int(bps + 0.5)
}
END {

}
