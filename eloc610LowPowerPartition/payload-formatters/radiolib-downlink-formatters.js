var colors = ["red", "green", "blue"];

function encodeDownlink(input) {
  return {
    bytes: [
      colors.indexOf(input.data.color),
      input.data.counter ,
      input.data.counter >> 8
          ],
    fPort: 4,
  };
}

function decodeDownlink(input) {
  switch (input.fPort) {
    case 4:
      return {
        data: {
          color: colors[input.bytes[0]],
        },
      };
    default:
      return {
        errors: ["unknown FPort"],
      };
  }
}
