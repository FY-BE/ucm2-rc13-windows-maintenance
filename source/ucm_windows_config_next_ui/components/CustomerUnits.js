.pragma library

function validForceN(newtonsText) {
    if (newtonsText === undefined || newtonsText === null
            || String(newtonsText).trim() === "--")
        return NaN
    const value = Number(newtonsText)
    return isFinite(value) ? value : NaN
}

function tonneText(newtonsText) {
    const value = validForceN(newtonsText)
    return isFinite(value) ? (value / 9806.65).toFixed(1) : "--"
}

function kilonewtonText(newtonsText) {
    const value = validForceN(newtonsText)
    return isFinite(value) ? (value / 1000.0).toFixed(1) : "--"
}

function microstrainText(newtonsText, rodDiameterMm) {
    // Formal strain must be provided by ARM, never inferred from host geometry.
    return "--"
}
