// canvas is the name of the HTML canvas element to draw on
// data is an array of objects. Each object is a data point with a
//   'time' field indicating the position on the x-axis and then
//   arbitrary additional fields for the y-axis for all the graphs
// graphs is an array of objects specifying metadata about each graph
//   to plot. The most important field is 'name' which indicates the
//   name of the field in the data array to use for the plot. Also specify
//   'color' which is a string defining the color of the line (CSS style).

function render_graph(canvas, data, graphs, start_time, now)
{
	var canvas = document.getElementById(canvas);
	var ctx = canvas.getContext('2d');
	
	// first find the highest rate, in order to scale
	var peak = 1000;
	for (dp in data)
	{
		for (g in graphs)
		{
			var n = graphs[g].name;
			if (!data[dp].hasOwnProperty(n)) continue;
			peak = Math.max(data[dp][n], peak);
		}
	}

	ctx.clearRect(0,0,canvas.width, canvas.height);
	ctx.save();

	var window_size = now - start_time;

	var view_width = canvas.width - 60;
	var view_height = canvas.height - 40;

	// the 0.5 is to align lines with pixels
	ctx.translate(0.5, 40.5);

	// used for text
	ctx.fillStyle = "black";
	ctx.textAlign = 'left';
	ctx.lineWidth = 1;

	// calculate the number of tics and the peak
	var num_tics = 10;
	var new_peak = 1;
	while (peak > new_peak)
		new_peak *= 10;
	num_tics = Math.ceil(peak / new_peak * 10);
	peak = new_peak * num_tics / 10;
	if (num_tics < 5) num_tics *= 2;

	var scalex = view_width / (now - start_time);
	var scaley = view_height / peak;

	ctx.save();
	// draw y-axis tics
	for (var i = 0; i < num_tics; ++i)
	{
		ctx.strokeStyle = "#000";
		ctx.setLineDash([]);
		var y = Math.floor(view_height * i / num_tics);
		ctx.beginPath();
		ctx.moveTo(view_width - 6, y);
		ctx.lineTo(view_width, y);
		ctx.lineTo(view_width, y + view_height / num_tics);
		ctx.stroke();
		var rate = peak - peak * i / num_tics;
		ctx.fillText((rate / 1000).toFixed( peak< 5000 ? 1 : 0) + ' kB/s', view_width + 2, y + 4);

		ctx.setLineDash([5]);
		ctx.strokeStyle = "#aaa";
		ctx.beginPath();
		ctx.moveTo(view_width - 6, y);
		ctx.lineTo(0, y);
		ctx.stroke();
	}
	ctx.restore();

	ctx.textAlign = 'right';

	// draw the graphs for all torrents
	for (k in graphs)
	{
		var g = graphs[k];

		ctx.strokeStyle = g.color;
		var first = true;
		ctx.beginPath();
		for (i in data)
		{
			var time = data[i].time;
			var y = data[i][g.name];
			if (typeof(y) == 'undefined') continue;
   
			if (first)
			{
				ctx.moveTo((time - start_time) * scalex, view_height - y * scaley);
				first = false;
			}
			else
			{
				ctx.lineTo((time - start_time) * scalex, view_height - y * scaley);
			}
		}
		ctx.stroke();
	}
	ctx.restore();
}

window['render_graph'] = render_graph;

